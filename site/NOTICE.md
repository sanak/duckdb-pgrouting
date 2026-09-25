# What this site serves, and under which licence

| Content | Source | Licence |
|---|---|---|
| The page and its code (`index.html`, `assets/*.js`, `assets/*.css`) | this repository's `site/` | MIT (`site/LICENSE`) |
| Bundled npm packages inside `assets/` (DuckDB-Wasm, Apache Arrow, MapLibre GL JS, …) | npm | their own licences, listed in `third-party-licenses.md` next to the page |
| `data/*.csv` — the sample graph | pgRouting's sample data, via this repository's `test/data/pgrouting_sample/` | GPL-2.0-or-later |
| `wasm/<duckdb version>/<variant>/pgrouting.duckdb_extension.wasm` | copied unchanged from this repository's GitHub Releases | GPL-2.0-or-later; each Release links the exact source commit it was built from |

The GPL-licensed data and binaries are served next to the MIT-licensed page as separate files; the
page loads them at run time and does not include them.

This site is not affiliated with or endorsed by the pgRouting project or OSGeo.
