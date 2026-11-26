/* Same low 24 pixel bits, four pixels per bounded 12-byte store. memcpy
 * permits arbitrary row/pixel alignment and avoids type-punning alias UB.
 * The target and all supported test hosts are little endian.
 */
static inline void
lithium_bgr24_store4(uint8_t *dest, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	const uint32_t words[3] = {
	    (a & 0x00ffffffu) | (b << 24),
	    ((b >> 8) & 0x0000ffffu) | (c << 16),
	    ((c >> 16) & 0x000000ffu) | (d << 8)
	};
	memcpy(dest, words, sizeof(words));
}
