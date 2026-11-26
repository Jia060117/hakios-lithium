/* HakiOS MiniAMFI has no OSEntitlements object. Use its existing, hash-checked
 * original XML blob, retaining csops' size probe and audit-token validation.
 * The blob belongs to the vnode: it must never be freed by this adapter. */
static int
lithium_csops_xml(struct cs_blob *blob, user_size_t size, user_addr_t address)
{
    void *xml = NULL;
    size_t bytes = 0;
    int error = csblob_get_entitlements(blob, &xml, &bytes);
    if (error != 0) return error;
    return csops_copy_token(xml, bytes, size, address);
}
