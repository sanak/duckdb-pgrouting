// SPDX-License-Identifier: MIT
// The result grid: Tabulator in range-selection mode, so a cell, a dragged range or a whole row
// (through the row-number column) copies as tab-separated text.
import {
  ClipboardModule,
  ExportModule,
  FormatModule,
  FrozenColumnsModule,
  InteractionModule,
  KeybindingsModule,
  SelectRangeModule,
  Tabulator,
} from 'tabulator-tables';
import 'tabulator-tables/dist/css/tabulator_simple.min.css';
import { gridData, type Plain, type ResultSet } from './result.ts';

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
}

// Tabulator puts a column title into innerHTML; a column name is SQL text and must stay text.
function textNode(text: string): HTMLElement {
  return Object.assign(document.createElement('span'), { textContent: text });
}

export function createResultGrid(container: HTMLElement): ResultGrid {
  let table: Tabulator | null = null;

  return {
    show(result, shown) {
      table?.destroy();
      table = null;
      const element = document.createElement('div');
      container.replaceChildren(element);
      const { columns, rows } = gridData(result, shown);
      if (columns.length === 0) return;

      const next = new Tabulator(element, {
        data: rows,
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
      });
      element.addEventListener('keydown', (event) => {
        if ((event.ctrlKey || event.metaKey) && event.key === 'c') {
          event.preventDefault();
          next.copyToClipboard();
        }
      });
      table = next;
    },
  };
}
