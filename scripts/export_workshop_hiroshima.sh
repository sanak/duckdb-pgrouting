#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Rebuilds test/data/workshop-hiroshima/{ways,configuration}.parquet the way the pgRouting
# workshop builds its database: osm2pgrouting with its default mapconfig.xml on the workshop's
# Hiroshima extract, then the workshop's renames (gid -> id, the_geom -> geom).
#
# A one-off, run by hand, never in CI. It needs Docker (PostgreSQL + PostGIS + osm2pgrouting run
# in a container) and a released DuckDB v1.5 CLI, which installs its postgres and spatial
# extensions on first use. The rule that the test tooling needs no PostgreSQL covers the Python
# tools under scripts/: this script produces committed data and no test runs it.
#
# Geometry is written as WKB in a plain BLOB column named geom, not as GeoParquet: DuckDB-Wasm
# cannot yet read GeoParquet's geometry column, and the Playground reads these files. Convert it
# with ST_GeomFromWKB(geom) after LOAD spatial.
#
# Usage: scripts/export_workshop_hiroshima.sh [path to a DuckDB v1.5 CLI, default: duckdb]
set -euo pipefail

IMAGE="postgis/postgis@sha256:01a6a70e41e6c4467c8f55f6063555ed72db2d6662cd0d571040d42eadaeb6f6"
OSM_URL="https://download.osgeo.org/pgrouting/workshops/HIROSHIMA_JP.osm.bz2"
OSM_SHA256="62c92113cef1eb553a785ce81c511683bb1c36886b0e286906f155ffc696d1b7"
DUCKDB="${1:-duckdb}"
CONTAINER="pgr-workshop-export"
PORT=55432
PASSWORD="export"
OUT="test/data/workshop-hiroshima"

cd "$(dirname "$0")/.."

version="$("$DUCKDB" -noheader -list -c "SELECT library_version FROM pragma_version()")"
case "$version" in
  v1.5.*) ;;
  *) echo "error: $DUCKDB reports $version; a DuckDB v1.5 CLI is required" >&2; exit 1 ;;
esac

work="$(mktemp -d)"
cleanup() {
  docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
  rm -rf "$work"
}
trap cleanup EXIT

psql_exec() {
  docker exec -e PGPASSWORD="$PASSWORD" "$CONTAINER" \
    psql -v ON_ERROR_STOP=1 -U postgres -h 127.0.0.1 "$@"
}

# The extract is pinned by hash: a changed snapshot upstream fails here, loudly.
curl -fsSL -o "$work/HIROSHIMA_JP.osm.bz2" "$OSM_URL"
echo "$OSM_SHA256  $work/HIROSHIMA_JP.osm.bz2" | shasum -a 256 -c -
bunzip2 "$work/HIROSHIMA_JP.osm.bz2"

# The image is published for linux/amd64 only.
docker run -d --name "$CONTAINER" --platform linux/amd64 -e POSTGRES_PASSWORD="$PASSWORD" \
  -p "127.0.0.1:$PORT:5432" -v "$work:/work" "$IMAGE" >/dev/null
# During initialisation the server listens on its socket only; TCP answers once it is final.
until docker exec "$CONTAINER" pg_isready -U postgres -h 127.0.0.1 >/dev/null 2>&1; do sleep 1; done

docker exec "$CONTAINER" bash -c "apt-get update -qq && apt-get install -y -qq osm2pgrouting >/dev/null"
docker exec "$CONTAINER" osm2pgrouting --version

psql_exec -c "CREATE DATABASE city_routing"
psql_exec -d city_routing -c "CREATE EXTENSION postgis"
docker exec "$CONTAINER" osm2pgrouting \
  -f /work/HIROSHIMA_JP.osm -c /usr/share/osm2pgrouting/mapconfig.xml \
  -d city_routing -U postgres -h 127.0.0.1 -W "$PASSWORD" --clean
psql_exec -d city_routing \
  -c "ALTER TABLE ways RENAME COLUMN gid TO id" \
  -c "ALTER TABLE ways RENAME COLUMN the_geom TO geom"

counts="$(psql_exec -d city_routing -At \
  -c "SELECT (SELECT count(*) FROM ways) || '|' || (SELECT count(*) FROM configuration)")"
if [ "$counts" != "32163|36" ]; then
  echo "error: ways|configuration = $counts, the workshop has 32163|36" >&2
  exit 1
fi

mkdir -p "$OUT"
"$DUCKDB" -c "
INSTALL postgres; LOAD postgres; INSTALL spatial; LOAD spatial;
ATTACH 'host=127.0.0.1 port=$PORT user=postgres password=$PASSWORD dbname=city_routing'
  AS pg (TYPE postgres, READ_ONLY);
COPY (SELECT * REPLACE (CAST(ST_AsWKB(geom) AS BLOB) AS geom) FROM pg.public.ways ORDER BY id)
  TO '$OUT/ways.parquet' (FORMAT parquet, COMPRESSION zstd);
COPY (SELECT * FROM pg.public.configuration ORDER BY id)
  TO '$OUT/configuration.parquet' (FORMAT parquet, COMPRESSION zstd);
"
"$DUCKDB" -c "
SELECT count(*) AS ways, any_value(typeof(geom)) AS geom_type
FROM read_parquet('$OUT/ways.parquet');
SELECT count(*) AS configuration FROM read_parquet('$OUT/configuration.parquet');
"
ls -l "$OUT"
