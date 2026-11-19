# Generated tables

These are the analysis results that the published builds were made with. They hold addresses, sizes, CRCs and
symbol names. They don't hold any bytes of the XBE: the start-up initialisers' bytes are made from your own copy (see
[docs/BUILD.md](../docs/BUILD.md)). `docs/BUILD.md` says where each file goes. The script that writes a file is given
in brackets.

| File | What it holds |
| --- | --- |
| `retail_syms.S` | One absolute symbol per data symbol of the game's source that stands for retail data, at its retail address (`gen.py`) |
| `retail_tables.template.S` | The XBE sections to load (with their CRCs) and the code pointers in the game's data to re-aim at the build's functions; `@@H2N_DELTA@@` marks where `make_retail_tables.py` puts the initialisers' writes (`gen.py`, then the clean-up in `fix_resolve.json`) |
| `retail_code.S` | The XBE's code sections (with their CRCs), the kernel imports, which functions run as the XBE's code, and which of the XBE's library functions jump to the build's own (`gen_code.py`) |
| `plan.json` | Which retail address each data symbol stands for (`plan.py`, then `statics2.py`, `vtables.py`, `add_globals.py`) |
| `oracle.json`, `oracle2.json`, `funcmap.json` | Symbol and retail-address pairs read from the matched functions, and their cross-check (`oracle.py`, `oracle2.py`). The published `oracle2.json` leaves out `libnames`, which are names from the SDK's libraries |
| `fixups.json`, `fixups2.json` | Pointers to code inside the game's data (`scan.py`, `scan2.py`). `gen.py` reads `fixups2.json` |
| `fix_resolve.json` | The hand clean-up of those pointers: a vector deleting destructor re-aimed to the scalar one, or the entry dropped |
| `statics_plan.json`, `statics_fields.json` | File-scope statics that are really a retail global reached by name from another file (`statics1.py`, `statics2.py`) |
| `vtables_plan.json` | Retail's vtable for classes the plan doesn't place, found by the functions both hold (`vtables.py`) |
| `extra_globals.json` | Data symbols that newly compiled source defines for retail data (read by `add_globals.py`) |
| `renamed_statics.json` | Statics renamed by `rewrite.py` |
| `unresolved.json`, `unresolved2.json`, `unresolved3.json` | The outside symbols the game's objects need. `unresolved3.json` is current (`undef3.py`) |
| `stub_returns.json`, `stub_types.json` | What the stand-ins for the Xbox functions the layer doesn't define return (read by `mkstubs3.py`) |
| `kernel_names.json` | The console kernel's exports by ordinal |
| `retail_routes.json`, `retail_routes_not.json` | Unproven functions sorted by how the XBE's code takes their arguments (`retailconv.py`) |
| `routes_called.json`, `routes_unproven.json`, `routes_pointer.json` (and the `_s9e` and `_not` variants) | Unproven drafts that run as the XBE's code instead (`unproven_routes.py`, `pointer_routes.py`) |
| `code_immediates.json` | Numbers in the XBE's code that name a game function (`code_immediates.py`) |
| `datacmp.json` | The source's initialised data compared with retail's (`datacmp.py`) |
| `init_table.json` | The start-up initialiser table (`findinit.py`) |
| `inline_missing.json`, `keep_manifest.json` | From an earlier stage, when cross-file inline functions were compiled separately (`inl.py`, `keep.py`) |
| `layer_lib/` | Empty archives for the `/DEFAULTLIB` names the game's objects carry |

## Game version 1.5

`../generated15/` holds 1.5's `retail_syms.S` and `retail_code.S` (the same kinds of tables, for the title update's
executable). 1.5's `retail_tables.S` and the other 1.5 tables are written by `scripts15/` from your own XBEs
(`chain15.sh`; see [docs/BUILD.md](../docs/BUILD.md)).
