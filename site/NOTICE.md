# What this site serves, and under which licence

| Content | Source | Licence |
|---|---|---|
| `index.html` and the site's own code in `assets/index-*.js` / `assets/index-*.css` | this repository's `site/` | MIT (`site/LICENSE`) |
| npm packages in `assets/`: the DuckDB-Wasm `.wasm` and worker files, the MapLibre worker, and the parts of `index-*.js` / `index-*.css` they supply (DuckDB-Wasm, Apache Arrow, MapLibre GL JS, Tabulator, …) | npm | their own licences, listed in `third-party-licenses.txt` next to the page |
| `data/index.json`, `data/*/dataset.json`, `data/sampledata/presets.json` | this repository's `site/datasets/` | MIT (`site/LICENSE`) |
| `data/sampledata/*.csv` — the sample graph | pgRouting's sample data, via this repository's `test/data/sampledata/` | GPL-2.0-or-later |
| `data/workshop-hiroshima/*.parquet` — central Hiroshima's road network | © OpenStreetMap contributors, prepared as the pgRouting workshop does (`data/workshop-hiroshima/NOTICE.md`) | [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/) |
| `data/workshop-hiroshima/presets.json` — the workshop's queries | adapted from the [pgRouting Workshop](https://workshop.pgrouting.org/), © pgRouting developers; changed for DuckDB (each changed query says how) | [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/), the licence of the workshop's repository. Its contributing guide calls the workshop's code GPL-2; this file follows the repository's only LICENSE file. |
| `data/overture/presets.json` — the Overture queries | adapted from `overture/overture.sql` in Crunchy Data's [crunchy-bridge-for-analytics-examples](https://github.com/CrunchyData/crunchy-bridge-for-analytics-examples/blob/fa3b6545e5cba0a44afdcd92d7995535ca20d979/overture/overture.sql), the example code of Paul Ramsey's article [Vehicle Routing with PostGIS and Overture Data](https://www.crunchydata.com/blog/vehicle-routing-with-postgis-and-overture-data); changed for DuckDB (each change says so in a comment) | MIT, © 2024 Crunchy Data (licence text below) |
| `wasm/<duckdb version>/<variant>/pgrouting.duckdb_extension.wasm` | copied unchanged from this repository's GitHub Releases | GPL-2.0-or-later; each Release links the exact source commit it was built from |

The GPL-licensed data and binaries, the ODbL data and the CC BY-SA presets are served next to the
MIT-licensed page as separate files; the page loads them at run time and does not include them.

Two things are not served by this site: duckdb-spatial, which DuckDB-Wasm downloads from DuckDB's
extension repository when a query runs `LOAD spatial` (MIT, bundling GEOS under LGPL-2.1 and PROJ
under MIT), and the basemap of the geographic maps, loaded from [OpenFreeMap](https://openfreemap.org/)
(© OpenMapTiles, data © OpenStreetMap contributors), whose credits the map shows.

## Overture Maps data

The Overture dataset stores no data on this site. When a reader runs its presets, the reader's
browser reads Overture Maps' transportation segments directly from Overture's servers: the release
catalogue from `stac.overturemaps.org` and the data files from
`overturemaps-us-west-2.s3.us-west-2.amazonaws.com`. Those servers see the reader's IP address.
The data is © OpenStreetMap contributors, Overture Maps Foundation, under
[ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/); the map shows this credit.

## Licence of overture.sql

`data/overture/presets.json` is adapted from
[overture.sql](https://github.com/CrunchyData/crunchy-bridge-for-analytics-examples/blob/fa3b6545e5cba0a44afdcd92d7995535ca20d979/overture/overture.sql),
distributed under this licence
([LICENSE.md](https://github.com/CrunchyData/crunchy-bridge-for-analytics-examples/blob/d09d90681a7eed3a9f49c3318d3181cdd17aff9f/LICENSE.md)):

    MIT License

    Copyright (c) 2024 Crunchy Data

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

The article's text and figures are not reproduced here; the presets link to it.

This site is not affiliated with or endorsed by the pgRouting project or OSGeo.
