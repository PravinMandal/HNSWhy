import { describe, expect, it } from 'vitest'
import {
  fileKind,
  formatBytes,
  formatCompact,
  formatMs,
  formatNumber,
  formatPercent,
  pluralize,
  truncate,
} from './format'

describe('formatNumber', () => {
  it('formats integers and respects fraction digits', () => {
    expect(formatNumber(1234)).toBe('1,234')
    expect(formatNumber(1.2345, 2)).toBe('1.23')
  })
})

describe('formatCompact', () => {
  it('uses compact notation above one thousand', () => {
    expect(formatCompact(999)).toBe('999')
    expect(formatCompact(12_400)).toBe('12.4K')
    expect(formatCompact(2_000_000)).toBe('2M')
  })
})

describe('formatPercent', () => {
  it('converts a ratio to a percentage', () => {
    expect(formatPercent(0.9926, 2)).toBe('99.26%')
    expect(formatPercent(1)).toBe('100.0%')
  })
})

describe('formatMs', () => {
  it('keeps sub-millisecond precision', () => {
    expect(formatMs(0.1486)).toBe('0.149 ms')
    expect(formatMs(4.2)).toBe('4.20 ms')
    expect(formatMs(42.25)).toBe('42.3 ms')
  })
})

describe('formatBytes', () => {
  it('scales to binary units and rejects empty values', () => {
    expect(formatBytes(0)).toBe('—')
    expect(formatBytes(-1)).toBe('—')
    expect(formatBytes(512)).toBe('512 B')
    expect(formatBytes(1536)).toBe('1.5 KB')
    expect(formatBytes(92.54 * 1024 * 1024)).toBe('92.5 MB')
  })
})

describe('fileKind', () => {
  it('classifies sources by extension', () => {
    expect(fileKind('paper.pdf')).toBe('PDF')
    expect(fileKind('notes.md')).toBe('Markdown')
    expect(fileKind('notes.markdown')).toBe('Markdown')
    expect(fileKind('notes.txt')).toBe('Text')
    expect(fileKind('paste')).toBe('Text')
  })
})

describe('pluralize', () => {
  it('uses the singular only for one', () => {
    expect(pluralize(1, 'document')).toBe('1 document')
    expect(pluralize(0, 'document')).toBe('0 documents')
    expect(pluralize(1200, 'chunk')).toBe('1,200 chunks')
  })
})

describe('truncate', () => {
  it('cuts at a word boundary when one is available', () => {
    expect(truncate('short', 10)).toBe('short')
    expect(truncate('the quick brown fox jumps', 15)).toBe('the quick brown…')
  })
})
