"""Bounded BGR24 adaptation of pinned HakiOS framebuffer; no display/GPU replacement."""
import difflib


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Framebuffer source anchor changed: ' + before[:80])
    return text.replace(before, after, 1)


def adapt(source):
    text = replace_once(source,
        'uint32_t        fPanelWidth, fPanelHeight, fRowBytes;',
        'uint32_t        fPanelWidth, fPanelHeight, fRowBytes, fPixelBytes;')
    text = replace_once(text,
        'vinfo.v_height == 0 || vinfo.v_depth != 32)',
        'vinfo.v_height == 0 ||\n'
        '\t    (vinfo.v_depth != 32 && vinfo.v_depth != 24) ||\n'
        '\t    (uint64_t) vinfo.v_rowbytes < (uint64_t) vinfo.v_width * (vinfo.v_depth / 8))')
    text = replace_once(text, 'fRowBytes = vinfo.v_rowbytes;',
        'fRowBytes = vinfo.v_rowbytes;\n\tfPixelBytes = vinfo.v_depth / 8;')
    text = replace_once(text,
        '\t\t\tmemcpy(to + px0, pixels + (size_t) (sy + y - py0) * stride + (size_t) sx * 4, (size_t) sw * 4);',
        '\t\t\tconst uint32_t * source = (const uint32_t *) (pixels + (size_t) (sy + y - py0) * stride) + sx;\n'
        '\t\t\tif (fPixelBytes == 3) {\n'
        '\t\t\t\tuint8_t * dest = fBase + (size_t) y * fRowBytes + (size_t) px0 * 3;\n'
        '\t\t\t\tfor (int32_t x = 0; x < sw; x++) {\n'
        '\t\t\t\t\tdest[x * 3] = source[x];\n'
        '\t\t\t\t\tdest[x * 3 + 1] = source[x] >> 8;\n'
        '\t\t\t\t\tdest[x * 3 + 2] = source[x] >> 16;\n'
        '\t\t\t\t}\n'
        '\t\t\t} else {\n'
        '\t\t\t\tmemcpy(to + px0, source, (size_t) sw * 4);\n'
        '\t\t\t}')
    text = replace_once(text, '\t\tif (fDiff) {\n\t\t\tfor (int32_t x = cx0; x < cx1; x++) {',
        '\t\t/* IOSurface stays BGRA32; only the physical panel storage is BGR24. */\n'
        '\t\tif (fPixelBytes == 3) {\n'
        '\t\t\tuint8_t * dest = fBase + (size_t) y * fRowBytes;\n'
        '\t\t\tfor (int32_t x = cx0; x < cx1; x++) {\n'
        '\t\t\t\tuint32_t value = filter ? blend(from[fColumn[x]], from[fColumn[x] + (fColumnWeight[x] != 0)], fColumnWeight[x]) : from[fColumn[x]];\n'
        '\t\t\t\tuint8_t * pixel = dest + (size_t) x * 3;\n'
        '\t\t\t\tif (fDiff && (pixel[0] != (uint8_t) value || pixel[1] != (uint8_t) (value >> 8) || pixel[2] != (uint8_t) (value >> 16))) {\n'
        '\t\t\t\t\tfDiffPixels++;\n'
        '\t\t\t\t\tfDiffLeft = (uint32_t) x < fDiffLeft ? (uint32_t) x : fDiffLeft;\n'
        '\t\t\t\t\tfDiffRight = (uint32_t) x > fDiffRight ? (uint32_t) x : fDiffRight;\n'
        '\t\t\t\t\tfDiffTop = (uint32_t) y < fDiffTop ? (uint32_t) y : fDiffTop;\n'
        '\t\t\t\t\tfDiffBottom = (uint32_t) y > fDiffBottom ? (uint32_t) y : fDiffBottom;\n'
        '\t\t\t\t}\n'
        '\t\t\t\tpixel[0] = value; pixel[1] = value >> 8; pixel[2] = value >> 16;\n'
        '\t\t\t}\n'
        '\t\t\tcontinue;\n'
        '\t\t}\n'
        '\t\tif (fDiff) {\n\t\t\tfor (int32_t x = cx0; x < cx1; x++) {')
    text = replace_once(text,
        'bzero(fBase + (size_t) y * fRowBytes, (size_t) fPanelWidth * 4);',
        'bzero(fBase + (size_t) y * fRowBytes, (size_t) fPanelWidth * fPixelBytes);')
    text = replace_once(text,
        'if (vinfo.v_baseaddr == 0 || vinfo.v_depth != 32) {',
        'if (vinfo.v_baseaddr == 0 || pixels == NULL ||\n'
        '\t    (vinfo.v_depth != 32 && vinfo.v_depth != 24) ||\n'
        '\t    (uint64_t) rowBytes < (uint64_t) columns * 4 ||\n'
        '\t    (uint64_t) vinfo.v_rowbytes < (uint64_t) vinfo.v_width * (vinfo.v_depth / 8)) {')
    text = replace_once(text,
        '\t\tmemcpy((void *) (vinfo.v_baseaddr + (size_t) y * vinfo.v_rowbytes), pixels + (size_t) y * rowBytes,\n'
        '\t\t    (size_t) columns * 4);',
        '\t\tuint8_t * dest = (uint8_t *) (vinfo.v_baseaddr + (size_t) y * vinfo.v_rowbytes);\n'
        '\t\tconst uint8_t * source = pixels + (size_t) y * rowBytes;\n'
        '\t\tif (vinfo.v_depth == 24) {\n'
        '\t\t\tfor (uint32_t x = 0; x < columns; x++) {\n'
        '\t\t\t\tmemcpy(dest + (size_t) x * 3, source + (size_t) x * 4, 3);\n'
        '\t\t\t}\n'
        '\t\t} else {\n'
        '\t\t\tmemcpy(dest, source, (size_t) columns * 4);\n'
        '\t\t}')
    return text


def patch(source):
    path = 'xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp'
    return ''.join(difflib.unified_diff(source.splitlines(keepends=True),
        adapt(source).splitlines(keepends=True), fromfile='a/' + path, tofile='b/' + path))
