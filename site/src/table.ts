// SPDX-License-Identifier: MIT
// The result grid: Tabulator in range-selection mode, so a cell, a dragged range or a whole row
// (through the row-number column) copies as tab-separated text. The edge selection shared with the
// map is a row class of the site's own, kept by the row formatter as rows scroll in and out.
import {
  ClipboardModule,
  ExportModule,
  FormatModule,
  FrozenColumnsModule,
  InteractionModule,
  KeybindingsModule,
  type RowComponent,
  SelectRangeModule,
  Tabulator,
} from 'tabulator-tables';
import 'tabulator-tables/dist/css/tabulator_simple.min.css';
import { edgeOf, gridData, type Plain, type ResultSet } from './result.ts';

// Only what range selection and copying need. SelectRange reads the frozen-columns module and the
// clipboard builds its text through the export module, so both are required too.
Tabulator.registerModule([
  SelectRangeModule,
  ClipboardModule,
  ExportModule,
  FormatModule,
  InteractionModule,
  KeybindingsModule,
  FrozenColumnsModule,
]);

export interface ResultGrid {
  show(result: ResultSet, shown: Plain[][]): void;
  select(edge: number | null, options: { scroll: boolean }): void;
  onRowPick(handler: (edge: number | null) => void): void;
}

// Tabulator puts a column title into innerHTML; a column name is SQL text and must stay text.
function textNode(text: string): HTMLElement {
  return Object.assign(document.createElement('span'), { textContent: text });
}

function edgeOfRow(row: RowComponent): number | null {
  const edge: unknown = row.getData().edge;
  return typeof edge === 'number' ? edge : null;
}

export function createResultGrid(container: HTMLElement): ResultGrid {
  let table: Tabulator | null = null;
  let selected: number | null = null;
  let pick: (edge: number | null) => void = () => {};

  const refresh = () => {
    for (const row of table?.getRows() ?? []) row.reformat();
  };

  return {
    show(result, shown) {
      table?.destroy();
      table = null;
      selected = null;
      const element = document.createElement('div');
      container.replaceChildren(element);
      const { columns, rows } = gridData(result, shown);
      if (columns.length === 0) return;

      const next = new Tabulator(element, {
        data: rows.map((cells, i) => ({ ...cells, edge: edgeOf(result, shown[i] ?? []) })),
        columns: columns.map(({ title, field }) => ({ title, field, titleFormatter: () => textNode(title) })),
        columnDefaults: { hozAlign: 'right' },
        layout: 'fitDataTable',
        maxHeight: '60vh',
        placeholder: 'No rows',
        selectableRange: 1,
        selectableRangeRows: true,
        rowHeader: { frozen: true, width: 44, hozAlign: 'center', formatter: 'rownum' },
        clipboard: 'copy',
        clipboardCopyRowRange: 'range',
        clipboardCopyStyled: false,
        clipboardCopyConfig: { rowHeaders: false, columnHeaders: false },
        // Plain text only: TSV pastes into editors and spreadsheets alike.
        clipboardCopyFormatter: (type, output) => (type === 'plain' ? output : ''),
        // Tabulator's own Ctrl+C binding reads the edit module unguarded and throws without it; the
        // listener below does the same through the public API.
        keybindings: { copyToClipboard: false },
        rowFormatter: (row) => {
          row.getElement().classList.toggle('edge-selected', selected !== null && edgeOfRow(row) === selected);
        },
        // Copying runs the row formatter on the export's own row copies, and Tabulator 6.5.3 does so by
        // overwriting getElement() on the live row component, which would leave the class above stuck
        // on the copied rows. Copied text carries no row styling anyway.
        rowFormatterClipboard: false,
      });
      next.on('cellClick', (_event, cell) => pick(edgeOfRow(cell.getRow())));
      element.addEventListener('keydown', (event) => {
        if ((event.ctrlKey || event.metaKey) && event.key === 'c') {
          event.preventDefault();
          next.copyToClipboard();
        }
      });
      table = next;
    },

    select(edge, { scroll }) {
      selected = edge;
      refresh();
      if (!scroll || edge === null) return;
      const first = table?.getRows().find((row) => edgeOfRow(row) === edge);
      if (first) void table?.scrollToRow(first, 'nearest', false);
    },

    onRowPick(handler) {
      pick = handler;
    },
  };
}
