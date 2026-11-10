const palette = [
  '#3f7f6b',
  '#5b6fb0',
  '#c06a3f',
  '#8a5fa8',
  '#a98328',
  '#3d8b9e',
  '#b0526b',
  '#6f8a3d',
]

export function buildDocumentColors(documentIds: number[]): Map<number, string> {
  const unique = [...new Set(documentIds)].sort((a, b) => a - b)
  const colors = new Map<number, string>()
  unique.forEach((id, index) => {
    colors.set(id, palette[index % palette.length])
  })
  return colors
}
