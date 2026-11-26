"""Inspect selected iOS 14 IPSW members from Apple's CDN using bounded ranges.

Uses an already saved locator.json as a URL locator, then verifies the actual
Apple-hosted BuildManifest. Does not download a complete IPSW or touch a phone.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import plistlib
import re
import urllib.parse
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts/research/ios14"


class AppleRangeFile(io.RawIOBase):
    def __init__(self, url, size):
        if urllib.parse.urlparse(url).hostname != "updates.cdn-apple.com":
            raise ValueError("Expected the Apple CDN")
        self.url, self.size, self.position, self.transferred = url, size, 0, 0
        self.requests = []

    def seekable(self):
        return True

    def tell(self):
        return self.position

    def seek(self, offset, whence=0):
        self.position = offset + (0 if whence == 0 else self.position if whence == 1 else self.size)
        if self.position < 0:
            raise ValueError("Negative seek")
        return self.position

    def read(self, size=-1):
        size = min(self.size - self.position, size if size >= 0 else self.size)
        if size <= 0:
            return b""
        if size > 32 * 1024 ** 2 or self.transferred + size > 64 * 1024 ** 2:
            raise ValueError("Bounded metadata download exceeded its limit")
        start, end = self.position, self.position + size - 1
        request = urllib.request.Request(self.url, headers={"Range": f"bytes={start}-{end}", "Accept-Encoding": "identity", "User-Agent": "ios14-lithium-research/1"})
        with urllib.request.urlopen(request, timeout=30) as response:
            expected = f"bytes {start}-{end}/{self.size}"
            if response.status != 206 or response.headers.get("Content-Range") != expected:
                raise ValueError("CDN did not honor the requested range; full download refused")
            data = response.read(size + 1)
        if len(data) != size:
            raise ValueError("Incorrect range length")
        self.position += size
        self.transferred += size
        self.requests.append({"offset": start, "size": size})
        return data


def plain(value):
    if isinstance(value, bytes):
        return value.hex()
    if isinstance(value, dict):
        return {key: plain(item) for key, item in value.items()}
    if isinstance(value, list):
        return [plain(item) for item in value]
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernelcache", action="store_true", help="Also fetch the selected compressed arm64 kernelcache")
    args = parser.parse_args()
    locator = json.loads((OUT / "locator.json").read_text())
    remote = AppleRangeFile(locator["url"], locator["filesize"])
    with zipfile.ZipFile(remote) as archive:
        manifest_bytes = archive.read("BuildManifest.plist")
        manifest = plistlib.loads(manifest_bytes)
        if manifest["ProductVersion"] != "14.8" or manifest["ProductBuildVersion"] != "18H17":
            raise ValueError("Unexpected OS version in Apple manifest")
        if "iPhone8,1" not in manifest["SupportedProductTypes"]:
            raise ValueError("Target product absent")
        (OUT / "BuildManifest.plist").write_bytes(manifest_bytes)
        identities = []
        for identity in manifest["BuildIdentities"]:
            info = identity["Info"]
            identities.append({"device_class": info["DeviceClass"], "chip_id": identity["ApChipID"],
                               "board_id": identity["ApBoardID"], "variant": info.get("Variant"),
                               "components": {key: plain(value) for key, value in identity["Manifest"].items()
                                              if key in ("KernelCache", "DeviceTree", "OS", "RestoreRamDisk", "StaticTrustCache")}})
        selected = next(item for item in identities if item["device_class"] == "n71ap")
        kernel_info = selected["components"]["KernelCache"]["Info"]
        report = {"source_url": locator["url"], "locator_source": "ipsw.me; used only to locate the Apple CDN asset",
                  "version": manifest["ProductVersion"], "build": manifest["ProductBuildVersion"],
                  "product_types": manifest["SupportedProductTypes"],
                  "manifest_sha256": hashlib.sha256(manifest_bytes).hexdigest(),
                  "selected_device_class": "n71ap", "identities": identities,
                  "full_ipsw_downloaded": False, "kernelcache_downloaded": False,
                  "exact_kernel_version": "pending decompression and version-string inspection"}
        if args.kernelcache:
            data = archive.read(kernel_info["Path"])
            (OUT / "kernelcache.im4p").write_bytes(data)
            report.update(kernelcache_downloaded=True, kernelcache_member=kernel_info["Path"],
                          kernelcache_im4p_sha256=hashlib.sha256(data).hexdigest(), kernelcache_im4p_bytes=len(data))
        report.update(range_bytes_transferred=remote.transferred, range_requests=remote.requests)
    target = ROOT / "reports/ios14-inputs.json"
    target.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(target)
    print("Verified Apple manifest:", report["version"], report["build"], report["selected_device_class"])
    print("Range bytes transferred:", remote.transferred)
    print("Kernelcache member:", kernel_info["Path"])


if __name__ == "__main__":
    main()
