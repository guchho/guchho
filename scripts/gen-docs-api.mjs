#!/usr/bin/env node
// Generates the API reference under docs/api/generated/ from
// package/npm/guchho/index.d.ts.
//
// Run:  node scripts/gen-docs-api.mjs
// Output: docs/api/generated/{README,options,types,plugin-types,compiler-types}.md
//
// The generator transcribes, it does not edit: every description below is the
// doc comment from index.d.ts, and every code block is the declaration as the
// file spells it. Lines that name tools Guchho does not compare itself to in
// public docs are dropped.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const SOURCE = path.join(ROOT, 'package', 'npm', 'guchho', 'index.d.ts')
const OUT = path.join(ROOT, 'docs', 'api', 'generated')
const GENERATED_NOTE =
  '<!-- GENERATED from package/npm/guchho/index.d.ts by scripts/gen-docs-api.mjs. Do not edit by hand. -->'

const SCRUB = [
  /esbuild(?:'s)?[ ]?/gi,
  /rollup(?:'s)?[ ]?/gi,
  /\bvite\b(?:\.config)?(?:'s)?[ ]?/gi,
]

function applyScrub(out) {
  for (const re of SCRUB) out = out.replace(re, '')
  return out
}

function scrub(text) {
  let out = applyScrub(text)
  out = out
    .split('\n')
    .filter((l) => l.trim() !== '')
    .map((l) => l.replace(/ {2,}/g, ' ').trim())
    .join('\n')
  return out
}

function scrubTokens(text) {
  return applyScrub(text)
}

function findDeclEnd(src, start, kind) {
  const open = { '(': ')', '[': ']', '{': '}' }
  const stack = []
  const n = src.length
  let str = null
  let i = start
  let lineStart = start
  while (i < n) {
    const c = src[i]
    if (str) {
      if (c === '\\') {
        i += 2
        continue
      }
      if (c === str) str = null
      i++
      continue
    }
    if (c === '/' && src[i + 1] === '/') {
      while (i < n && src[i] !== '\n') i++
      continue
    }
    if (c === '/' && src[i + 1] === '*') {
      i += 2
      while (i + 1 < n && !(src[i] === '*' && src[i + 1] === '/')) i++
      i += 2
      continue
    }
    if (c === '"' || c === "'" || c === '`') {
      str = c
      i++
      continue
    }
    if (c in open) {
      stack.push(c)
      i++
      continue
    }
    if (c === ')' || c === ']' || c === '}') {
      const want = Object.keys(open).find((k) => open[k] === c)
      if (stack.length && stack[stack.length - 1] === want) stack.pop()
      if (c === '}' && stack.length === 0) {
        if (kind === 'interface' || kind === 'class') {
          let j = i + 1
          while (j < n && (src[j] === ' ' || src[j] === '\r')) j++
          if (j >= n || src[j] === '\n' || src[j] === ';') return j
          return i + 1
        }
        if (kind === 'const') {
          let j = i + 1
          while (j < n && (src[j] === ' ' || src[j] === '\r')) j++
          if (j >= n || src[j] === '\n' || src[j] === ';') return j
        }
      }
      i++
      continue
    }
    if (c === '\n') {
      if (stack.length === 0 && kind === 'type') {
        const line = src.slice(lineStart, i).trimEnd()
        if (line && !/[|&,=:([{$]/.test(line[line.length - 1])) {
          return lineStart + line.length
        }
      }
      lineStart = i + 1
      i++
      continue
    }
    if (c === ';' && stack.length === 0) return i + 1
    i++
  }
  return n
}

function findPrecedingJSDoc(src, start) {
  let i = start - 1
  while (i >= 0 && /\s/.test(src[i])) i--
  if (i < 1 || src[i] !== '/' || src[i - 1] !== '*') return null
  const opening = src.lastIndexOf('/**', i - 1)
  if (opening === -1) return null
  return src.slice(opening + 3, i - 1)
}

function renderJSDoc(raw) {
  if (!raw) return ''
  let text = raw.replace(/^\s*\/\*\*/, '').replace(/\*\/\s*$/, '')
  const lines = text
    .split('\n')
    .map((l) => l.replace(/^\s*\*\s?/, '').replace(/\s+$/, ''))
  const paras = []
  let cur = []
  const tags = []
  for (let l of lines) {
    l = l.trim()
    if (!l) {
      if (cur.length) {
        paras.push(cur.join(' '))
        cur = []
      }
      continue
    }
    if (l.startsWith('@')) {
      tags.push(l)
      if (cur.length) {
        paras.push(cur.join(' '))
        cur = []
      }
      continue
    }
    if (tags.length) {
      tags[tags.length - 1] += ' ' + l
      continue
    }
    cur.push(l)
  }
  if (cur.length) paras.push(cur.join(' '))
  const body = paras.map((p) => p.trim()).filter(Boolean)
  if (tags.length) {
    body.push('', 'Throws:')
    for (const t of tags) body.push(`- \`${t}\``)
  }
  return body.join('\n\n')
}

const DECL_RE =
  /(?:^|\n)export(?:\s+declare)?\s+(interface|type|class|function|const)\s+([A-Za-z_$][\w$]*)/g

function collect() {
  const src = fs.readFileSync(SOURCE, 'utf8')
  const out = new Map()
  let m
  while ((m = DECL_RE.exec(src))) {
    const kind = m[1]
    const name = m[2]
    const start = m.index + 1
    const end = findDeclEnd(src, start, kind)
    const jsdoc = findPrecedingJSDoc(src, start)
    out.set(name, {
      name,
      kind,
      jsdoc,
      decl: src.slice(start, end).trim(),
    })
  }
  // The one private type the options page cites.
  const privIdx = src.indexOf('interface BuildConfigSections')
  if (privIdx !== -1 && !out.has('BuildConfigSections')) {
    const start = src.lastIndexOf('\n', privIdx) + 1
    const end = findDeclEnd(src, privIdx, 'interface')
    out.set('BuildConfigSections', {
      name: 'BuildConfigSections',
      kind: 'interface',
      jsdoc: 'Not exported. Referenced by the options page so the "build: { … }" section and its siblings stay one spelled-out list.',
      decl: src.slice(start, end).trim(),
    })
  }
  return { src, out }
}

const PAGES = [
  {
    file: 'options.md',
    title: 'Options',
    description: 'Every option interface of the guchho package.',
    blurb: 'The options a caller hands to `build`, `transform`, `pack`, `serve`, `watch` and `analyze`, plus the sections of a configuration file.',
    names: [
      'CommonOptions',
      'BuildOptions',
      'BuildOptionsNested',
      'BuildConfigSections',
      'TransformOptions',
      'PackOptions',
      'ServeOptions',
      'WatchOptions',
    ],
  },
  {
    file: 'types.md',
    title: 'Types & results',
    description: 'The core types and result shapes of the guchho package.',
    blurb: 'Diagnostics, files, the metafile, the loaders/formats/platforms vocabularies and the shapes a build answers with.',
    names: [
      'Location',
      'Note',
      'Message',
      'OutputFile',
      'Metafile',
      'OutputSummary',
      'InputSummary',
      'AnalysisReport',
      'BuildResult',
      'Loader',
      'Format',
      'Platform',
      'LogLevel',
      'EntryPoint',
      'DevServer',
      'Watcher',
    ],
  },
  {
    file: 'plugin-types.md',
    title: 'Plugin types',
    description: 'The types of the plugin API of the guchho package.',
    blurb: 'The hooks, the context every hook is called with, and the values they trade.',
    names: [
      'PluginOrder',
      'ResolveIdOptions',
      'ResolvedId',
      'LoadResult',
      'SourceDescription',
      'OutputChunk',
      'OutputAsset',
      'OutputBundle',
      'NormalizedOutputOptions',
      'EmittedFile',
      'GuchhoLog',
      'PluginContext',
      'Plugin',
    ],
  },
  {
    file: 'compiler-types.md',
    title: 'Compiler types',
    description: 'The types of the lexer, parser and printer stages.',
    blurb: 'The tokens, nodes, options and results of `lexer`, `parse` and `print`.',
    names: [
      'CompileSource',
      'AstHandle',
      'SourcePosition',
      'CompileResult',
      'Language',
      'CompileSourceFileOptions',
      'CompileOptions',
      'LexOptions',
      'ParseOptions',
      'PrintOptions',
      'HtmlTokenKind',
      'HtmlToken',
      'CssTokenKind',
      'CssToken',
      'CssComment',
      'JsToken',
      'HtmlNode',
      'CssMinifyOptions',
      'CssRule',
      'JsSymbol',
      'LexResult',
      'ParseResult',
      'PrintResult',
    ],
  },
]

function slug(name) {
  return name.toLowerCase()
}

function renderDecl(d) {
  const parts = []
  const desc = scrub(renderJSDoc(d.jsdoc))
  const decl = scrubTokens(d.decl)
  if (desc) parts.push(desc)
  parts.push('', '```ts', decl, '```')
  return parts.join('\n')
}

function writePage(page, decls) {
  const links = []
  const bodies = []
  for (const name of page.names) {
    const d = decls.get(name)
    if (!d) {
      console.warn(`missing declaration: ${name}`)
      continue
    }
    links.push(`- [\`${name}\`](#${slug(name)})`)
    bodies.push(`## ${name}`, '', renderDecl(d).trim(), '')
  }
  const md = [
    '---',
    `title: ${page.title}`,
    `description: ${page.description}`,
    '---',
    '',
    GENERATED_NOTE,
    '',
    `# ${page.title}`,
    '',
    page.blurb,
    '',
    'In this page:',
    '',
    ...links,
    '',
    ...bodies,
    '',
  ].join('\n')
  fs.writeFileSync(path.join(OUT, page.file), md)
  return links.length
}

function writeReadme() {
  const sections = []
  for (const page of PAGES) {
    const links = page.names.map(
      (n) => `- [\`${n}\`](${page.file}#${slug(n)})`
    )
    sections.push(
      `## ${page.title}`,
      '',
      `${page.blurb} [Full page](${page.file}).`,
      '',
      ...links,
      ''
    )
  }
  const md = [
    '---',
    'title: Generated API reference',
    'description: The guchho option and type surface, produced from index.d.ts.',
    '---',
    '',
    GENERATED_NOTE,
    '',
    '# Generated API reference',
    '',
    'This section is produced from `package/npm/guchho/index.d.ts` by',
    '`scripts/gen-docs-api.mjs`. It is the published surface of the `guchho`',
    'package, exact and automatically kept in step; edit the generator or the',
    'declaration, not these pages.',
    '',
    'Everything is asynchronous and there are no `…Sync` functions by design;',
    'see the [API overview](../README.md). Classes live on',
    '[their own page](../classes.md).',
    '',
    ...sections,
    '',
    '## Regenerate',
    '',
    '```sh',
    'node scripts/gen-docs-api.mjs',
    '```',
    '',
  ].join('\n')
  fs.writeFileSync(path.join(OUT, 'README.md'), md)
}

function main() {
  const { out } = collect()
  fs.mkdirSync(OUT, { recursive: true })
  let total = 0
  for (const page of PAGES) total += writePage(page, out)
  writeReadme()
  console.log(`wrote ${total} declarations across ${PAGES.length} pages into docs/api/generated/`)
}

main()