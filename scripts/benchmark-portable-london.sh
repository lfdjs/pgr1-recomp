#!/usr/bin/env bash
# EN/PT-BR: Isolated, no-build workload runner / Execução isolada sem compilar.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ $# -lt 3 || $# -gt 5 ]]; then
  echo 'Usage: benchmark-portable-london.sh BINARY SEED_RUN NEW_RUN [exact|audit|tracked] [SECONDS]' >&2
  exit 2
fi
binary="$(realpath -- "$1")"
seed="$(realpath -- "$2")"
run="$(realpath -m -- "$3")"
mode="${4:-exact}"
seconds="${5:-150}"
[[ -x "$binary" && -d "$seed/hdd" ]] || { echo 'Missing executable or seed HDD / Executável ou HDD seed ausente' >&2; exit 2; }
case "$mode" in exact|audit|tracked) ;; *) echo 'Invalid coherency mode' >&2; exit 2 ;; esac
[[ "$seconds" =~ ^[0-9]+$ && "$seconds" -ge 1 ]] || exit 2
[[ ! -e "$run" ]] || { echo "Run already exists / Execução já existe: $run" >&2; exit 2; }
mkdir -p -- "$run/logs"
cp -a --reflink=auto -- "$seed/hdd" "$run/hdd"
if [[ -d "$seed/shadercache-vk-v1" ]]; then
  cp -a --reflink=auto -- "$seed/shadercache-vk-v1" "$run/shadercache-vk-v1"
fi
export SCRIPT="${PGR1_BENCH_SCRIPT:-$root/scripts/pad/portable-london-easy.txt}"
export PGR1_CONFIG_FILE="$run/linux.conf"
export PGR1_VK_DEVICE="${PGR1_VK_DEVICE:-auto}" PGR1_SKIP_INTRO=1 PGR1_SCRIPT_ONLY=1
export OUTPUT=640x480 PGR1_FULLSCREEN=0 PGR1_SHOW_FPS=1 FPS=60 PGR1_FPS=60
export PGR1_VK_MSAA=0 PGR1_VK_FXAA=1 PGR1_VK_INTERNAL_PERCENT=100
export PGR1_VK_TEXTURE_COHERENCY="$mode" PGR1_VK_TEXTURE_OVERLAP=0
export PGR1_RUN_SECONDS="$seconds" PGR1_CAPTURE=final.ppm
unset PGR1_VK_TEXTURE_DIAGNOSTIC XBE_VISIBLE_ALL XBE_REPORTS_EXACT
python3 - "$root" "$binary" "$seed" "$run" "$mode" <<'PY'
import hashlib, json, os, pathlib, subprocess, sys
root, binary, seed, run, mode = sys.argv[1:]
def sha(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()
def git(*args):
    return subprocess.check_output(['git', '-C', root, *args], text=True).strip()
files = sorted(p for p in pathlib.Path(seed, 'hdd').rglob('*') if p.is_file())
hdd = hashlib.sha256()
for path in files:
    hdd.update(str(path.relative_to(pathlib.Path(seed, 'hdd'))).encode())
    hdd.update(bytes.fromhex(sha(path)))
keys = ('SCRIPT','OUTPUT','FPS','PGR1_RUN_SECONDS','PGR1_VK_DEVICE','PGR1_VK_MSAA',
        'PGR1_VK_FXAA','PGR1_VK_INTERNAL_PERCENT','PGR1_VK_TEXTURE_COHERENCY',
        'PGR1_VK_TEXTURE_JOURNAL_AUDIT','PGR1_VK_SYNC_PROFILE','PGR1_VK_SCENE_VBLANKS',
        'PGR1_VK_PIPELINED','PGR1_VK_SHADER_WORKERS')
metadata = dict(revision=git('rev-parse','HEAD'), sdk_revision=git('-C','sdk','rev-parse','HEAD'),
    dirty=git('status','--short'), binary=binary, binary_sha256=sha(binary),
    seed=seed, seed_hdd_sha256=hdd.hexdigest(), script_sha256=sha(os.environ['SCRIPT']),
    environment={k:os.environ.get(k) for k in keys},
    scene_verified=False, complete_race=False,
    note='Verify London/Westminster at guest vblank 1400; straight-input workload is not a full race.')
pathlib.Path(run, 'benchmark.json').write_text(json.dumps(metadata, indent=2)+'\n')
PY
cd -- "$run"
"$binary" "$root/assets" > console.log 2>&1
python3 "$root/portable/tools/frame_report.py" "$run" --target-fps 60 --window 120 > frame-report.txt
python3 "$root/portable/tools/texture_report.py" "$run" --window 300 > texture-report.txt
if [[ -f "$run/sync.csv" ]]; then
  python3 "$root/portable/tools/sync_report.py" "$run" > sync-report.txt
fi
