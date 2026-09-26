# workshop-hiroshima data

`ways.parquet` and `configuration.parquet` are the road network of central Hiroshima, Japan, as the
[pgRouting workshop](https://workshop.pgrouting.org/) (FOSS4G Hiroshima) loads it into PostgreSQL.

- **Source:** the OpenStreetMap extract `HIROSHIMA_JP.osm.bz2` published at
  <https://download.osgeo.org/pgrouting/workshops/HIROSHIMA_JP.osm.bz2> (snapshot of 2026-06-14,
  SHA-256 `62c92113cef1eb553a785ce81c511683bb1c36886b0e286906f155ffc696d1b7`).
- **How it was made:** `scripts/export_workshop_hiroshima.sh` imports the extract with
  osm2pgrouting 2.3.7 and its default `mapconfig.xml`, applies the workshop's renames
  (`gid` → `id`, `the_geom` → `geom`), and writes both tables with DuckDB. Vertex ids are
  osm2pgrouting's, so they match the workshop's (for example 7508, 4128, 4632, 2110, 1688).
- **Geometry:** `geom` holds each way as WKB (longitude/latitude, EPSG:4326) in a BLOB column. With
  the spatial extension loaded, `ST_GeomFromWKB(geom)` turns it into a GEOMETRY.
- **Licence:** © OpenStreetMap contributors. This data is available under the
  [Open Database License 1.0](https://opendatacommons.org/licenses/odbl/1-0/). These files are a
  derivative database of OpenStreetMap, and the script above is the method that produced them.
  The repository's own licences (GPL-2.0-or-later for the extension, MIT for `site/`) do not
  apply to them.
