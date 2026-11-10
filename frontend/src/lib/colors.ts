// Twelve distinguishable hues, each dark enough to clear 3:1 against the
// light map canvas and light enough to clear 3:1 against the dark one.
const palette = [
  '#3f7f6b',
  '#5b6fb0',
  '#c06a3f',
  '#8a5fa8',
  '#9c7623',
  '#3d8b9e',
  '#b0526b',
  '#6f8a3d',
  '#b0433a',
  '#3b6b9b',
  '#4a7a35',
  '#a63a8a',
]

export function buildDocumentColors(documentIds: number[]): Map<number, string> {
  const unique = [...new Set(documentIds)].sort((a, b) => a - b)
  const colors = new Map<number, string>()
  unique.forEach((id, index) => {
    colors.set(id, palette[index % palette.length])
  })
  return colors
}
