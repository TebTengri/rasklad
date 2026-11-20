#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Offline bounded project navigation; never runs build/tests or reads ignored inputs."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parent.parent
CURATED=ROOT/'docs/project-map.json'
INDEX=ROOT/'docs/project-graph.json'
ROOT_FILES={'AGENTS.md','CLAUDE.md','SECURITY.md','THIRD_PARTY.md','rasklad.pro','README.md','LICENSE'}

def contained(path):
    path=Path(path)
    relative=path.relative_to(ROOT)
    current=ROOT
    for part in relative.parts:
        if part in {'.','..'}:raise ValueError('invalid navigation path')
        current/=part
        if current.is_symlink():raise ValueError('symlink navigation path')
    if not path.resolve().is_relative_to(ROOT):raise ValueError('navigation path escape')
    return path

SUFFIXES={'.py','.cpp','.h','.hpp','.mjs','.js','.qml','.pro','.md'}

def sources():
    raw=subprocess.run(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=ROOT,
                       check=True,stdout=subprocess.PIPE).stdout
    result={}
    for name in sorted(set(raw.decode().split('\0'))):
        p=ROOT/name
        if (not name or not (name.startswith(('src/','tools/','tests/','bin/','docs/')) or name in ROOT_FILES)
                or name.startswith('docs/knowledge/') or name in {'docs/project-graph.json','docs/PROJECT-MAP.md'}
                or (p.suffix not in SUFFIXES and not name.startswith('bin/') and name not in ROOT_FILES)):continue
        if p.is_symlink() or not p.is_file() or p.stat().st_size>262144:continue
        if not p.resolve().is_relative_to(ROOT):continue
        result[name]=p.read_bytes()
    return result

def digest(payload):return hashlib.sha256(payload).hexdigest()

def build():
    contained(INDEX);contained(CURATED);contained(ROOT/'docs/knowledge')
    inventory=sources();files=[];edges=[]
    for name,payload in inventory.items():
        text=payload.decode('utf-8',errors='replace');symbols=[]
        if name.endswith('.py'):
            try:
                tree=ast.parse(text)
                for node in ast.walk(tree):
                    if isinstance(node,(ast.FunctionDef,ast.AsyncFunctionDef,ast.ClassDef)):
                        symbols.append({'name':node.name,'line':node.lineno,'kind':type(node).__name__})
            except SyntaxError:pass
        files.append({'path':name,'symbols':symbols})
        patterns=[r'^\s*#include\s+"([^"]+)"',r'(?:from\s+|import\s*)[\'"]([^\'\"]+)[\'\"]']
        for pattern in patterns:
            for match in re.finditer(pattern,text,re.M):
                target=match.group(1)
                if target.startswith('file:'):continue
                candidate=(ROOT/name).parent/target
                resolved=candidate.resolve()
                if not resolved.is_relative_to(ROOT):continue
                relative=str(resolved.relative_to(ROOT))
                if relative not in inventory:
                    candidates=[x for x in inventory if x=='src/'+target]
                    if len(candidates)!=1:continue
                    relative=candidates[0]
                edges.append({'from':name,'to':relative,'kind':'literal_include_or_import',
                              'line':text[:match.start()].count('\n')+1})
    value={'schema':1,'scope':'File index and literal references, NOT exhaustive call graph. Python symbols from AST; no execution.',
           'curatedSha256':digest(CURATED.read_bytes()),
           'sourceSha256':{name:digest(data) for name,data in inventory.items()},'files':files,'edges':edges}
    vault=ROOT/'docs/knowledge';vault.mkdir(exist_ok=True)
    curated=json.loads(CURATED.read_text())
    names=[]
    for node in curated['nodes']:
        key=node['id']
        if not re.fullmatch('[a-z0-9_-]+',key):raise ValueError('invalid node id')
        names.append(key)
        lines=['# '+node['title'],'',node.get('notes',''),'','Статус: '+node['status'],'','## Файлы','']
        lines+=['- `'+path+'`' for path in node['paths']]
        lines+=['','## Связи','']
        for edge in curated['edges']:
            if edge['from']==key:lines.append(f"- [{edge['to']}]({edge['to']}.md): {edge['relation']}")
            elif edge['to']==key:lines.append(f"- [{edge['from']}]({edge['from']}.md): incoming {edge['relation']}")
        if node['tests']:lines+=['','## Проверки','']+['- `'+command+'`' for command in node['tests']]
        lines+=['','Снимок из `docs/project-map.json`; перед решениями проверьте `bin/project-map check`.','']
        contained(vault/(key+'.md')).write_text('\n'.join(lines))
    lines=['# rasklad — карта заметок','','Откройте именно `docs/knowledge` как существующее хранилище Obsidian.','Здесь только производные публичные заметки; build/, профили и корпуса сюда не входят.','Исходный индекс и команды — в репозитории rasklad, а не в настройках Obsidian.','','## Компоненты','']
    lines += [f'- [{n["title"]}]({n["id"]}.md)' for n in curated['nodes']]
    lines+=['','Обновление: `bin/project-map build`. Производные страницы не редактировать вручную;','решения и статусы изменять в `docs/project-map.json`, затем обновить карту.','Генератор не включает sync и plugins Obsidian; настройки хранилища (.obsidian/) не коммитятся.','']
    contained(vault/'Start.md').write_text('\n'.join(lines))
    value['vaultSha256']={str(p.relative_to(ROOT)):digest(contained(p).read_bytes()) for p in sorted(vault.glob('*.md'))}
    INDEX.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps({'files':len(files),'literalEdges':len(edges),'components':len(names),'vaultPages':len(names)+1}))

def check():
    contained(INDEX);contained(CURATED)
    if not INDEX.exists():print('Index absent; run bin/project-map build');return 1
    value=json.loads(INDEX.read_text());inventory={n:digest(p) for n,p in sources().items()}
    stale=sorted(n for n in set(inventory)|set(value['sourceSha256']) if inventory.get(n)!=value['sourceSha256'].get(n))
    if digest(CURATED.read_bytes())!=value['curatedSha256']:stale.append('docs/project-map.json')
    for name,expected in value.get('vaultSha256',{}).items():
        if not isinstance(name,str) or not name.startswith('docs/knowledge/') or not name.endswith('.md'):raise ValueError('invalid vault index path')
        p=contained(ROOT/name)
        if not p.is_file() or p.is_symlink() or digest(p.read_bytes())!=expected:stale.append(name)
    print(json.dumps({'fresh':not stale,'changed':stale[:20],'changedCount':len(stale)}));return int(bool(stale))

def query(term='',file=None):
    curated=json.loads(contained(CURATED).read_text());term=term.casefold()
    matches=[n for n in curated['nodes'] if not term or term in json.dumps(n,ensure_ascii=False).casefold()]
    if file:matches=[n for n in curated['nodes'] if file in n['paths']]
    print('Navigation only; status is a snapshot.')
    check()
    for node in matches[:8]:
        print(f"\n{node['id']}: {node['title']} [{node['status']}]\n{node.get('notes','')}")
        for path in node['paths'][:8]:print('  '+path)
        if node['tests']:print('  checks (NOT executed): '+'; '.join(node['tests'][:3]))
    if file and INDEX.exists():
        graph=json.loads(contained(INDEX).read_text());info=next((x for x in graph['files'] if x['path']==file),None)
        if info:
            for symbol in info['symbols'][:20]:print(f"  {symbol['name']} :{symbol['line']}")
            for edge in [e for e in graph['edges'] if file in (e['from'],e['to'])][:15]:print(f"  {edge['from']} -> {edge['to']}")
    if not matches:print('No curated match; use targeted rg in the project.')

def main():
    parser=argparse.ArgumentParser(description=__doc__);sub=parser.add_subparsers(dest='command',required=True)
    sub.add_parser('build');sub.add_parser('check');sub.add_parser('overview')
    p=sub.add_parser('find');p.add_argument('term');p=sub.add_parser('file');p.add_argument('path')
    args=parser.parse_args()
    if args.command=='build':build();return 0
    if args.command=='check':return check()
    query(getattr(args,'term',''),getattr(args,'path',None));return 0
if __name__=='__main__':sys.exit(main())
