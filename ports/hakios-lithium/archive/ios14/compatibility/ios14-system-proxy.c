/* Called once before userland, only for the lithium_ui boot contract. The
 * complete persona subsystem owns the new list reference and performs normal
 * lookup, spawn adoption and credential handling. No find result is forged. */
static int
lithium_system_proxy_bootstrap(uid_t *created_id)
{
    struct persona *found = NULL;
    size_t count = 1;
    int error = persona_find_all(NULL, PERSONA_ID_NONE, PERSONA_SYSTEM_PROXY, &found, &count);
    if (error == 0) {
        if (found == NULL) return ESRCH;
        *created_id = found->pna_id;
        persona_put(found);
        return count == 1 ? 0 : EEXIST;
    }
    if (error != ESRCH) return error;
    found = persona_alloc(PERSONA_ID_NONE, "system-proxy", PERSONA_SYSTEM_PROXY,
        NULL, KAUTH_UID_NONE, &error);
    if (found == NULL) return error != 0 ? error : ENOMEM;
    error = persona_init_begin(found);
    persona_init_end(found, error);
    if (error != 0) {
        persona_put(found);
        return error;
    }
    *created_id = found->pna_id;
    /* Keep the allocation's list reference; release only this lookup's ref. */
    struct persona *check = persona_lookup(*created_id);
    if (check == NULL) return ESRCH;
    error = check->pna_type == PERSONA_SYSTEM_PROXY ? 0 : EINVAL;
    persona_put(check);
    return error;
}
