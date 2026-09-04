# Texture sources

The companion repository stores editable replacement images here. Use the
exact 16-hex RT64 v5 hash from the dump as the filename, for example
`9a54e91197135650.png`. Keep the source composition and tile dimensions
compatible with the original texture; changing UV/layout semantics belongs in
the renderer, not in a texture replacement.

For this port, source captures are runtime-driven. Exercise every menu, HUD
state, car, and circuit before selecting replacements. Raw `.tmem`, `.rdram`,
palette dumps, ROMs, and decoded scratch output stay local and must not be
copied into the companion repository.
