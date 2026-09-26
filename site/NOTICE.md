# What this site serves, and under which licence

| Content | Source | Licence |
|---|---|---|
| `index.html` and the site's own code in `assets/index-*.js` / `assets/index-*.css` | this repository's `site/` | MIT (`site/LICENSE`) |
| npm packages in `assets/`: the DuckDB-Wasm `.wasm` and worker files, the MapLibre worker, and the parts of `index-*.js` / `index-*.css` they supply (DuckDB-Wasm, Apache Arrow, MapLibre GL JS, Tabulator, …) | npm | their own licences, listed in `third-party-licenses.txt` next to the page |
| `data/index.json`, `data/*/dataset.json`, `data/sampledata/presets.json` | this repository's `site/datasets/` | MIT (`site/LICENSE`) |
| `data/sampledata/*.csv` — the sample graph | pgRouting's sample data, via this repository's `test/data/sampledata/` | GPL-2.0-or-later |
| `data/workshop-hiroshima/*.parquet` — central Hiroshima's road network | © OpenStreetMap contributors, prepared as the pgRouting workshop does (`data/workshop-hiroshima/NOTICE.md`) | [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/) |
| `data/workshop-hiroshima/presets.json` — the workshop's queries | adapted from the [pgRouting Workshop](https://workshop.pgrouting.org/), © pgRouting developers; changed for DuckDB (each changed query says how) | [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/), the licence of the workshop's repository. Its contributing guide calls the workshop's code GPL-2; this file follows the repository's only LICENSE file. |
| `wasm/<duckdb version>/<variant>/pgrouting.duckdb_extension.wasm` | copied unchanged from this repository's GitHub Releases | GPL-2.0-or-later; each Release links the exact source commit it was built from |

The GPL-licensed data and binaries, the ODbL data and the CC BY-SA presets are served next to the
MIT-licensed page as separate files; the page loads them at run time and does not include them.

Two things are not served by this site: duckdb-spatial, which DuckDB-Wasm downloads from DuckDB's
extension repository when a query runs `LOAD spatial` (MIT, bundling GEOS under LGPL-2.1 and PROJ
under MIT), and the basemap of the Hiroshima map, loaded from [OpenFreeMap](https://openfreemap.org/)
(© OpenMapTiles, data © OpenStreetMap contributors), whose credits the map shows.

This site is not affiliated with or endorsed by the pgRouting project or OSGeo.
