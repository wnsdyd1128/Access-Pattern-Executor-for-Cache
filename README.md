# APEX-Cache

**Access Pattern Executor for Cache** — LLVM IR 기반 정적 캐시 분석 도구.

[Access-Pattern-Extractor](https://github.com/OBC-SIM/Access-Pattern-Extractor)가 생성한
AP JSON을 입력으로 받아, 프로그램 실행 전에 캐시 동작을 시뮬레이션하고
Region·core·객체·접근 명령 단위로 miss를 귀속한다.

---

## 개요

```
C/C++ 소스 코드
  → Access-Pattern-Extractor  (frontend/ 서브모듈)
  → AP JSON
  → APEX-Cache
      ├─ AP Layer       : JSON 파싱 → AccessEvent 스트림 복원
      ├─ Memory Layer   : flat address 배치 → byte_address 계산
      ├─ Cache Layer    : XML 설정형 multi-L1/L2/Memory 시뮬레이션
      ├─ Analysis Layer : cold/capacity/conflict miss 귀속 및 진단
      └─ Report Layer   : CSV / JSON / Markdown 출력
```

기존 Cachegrind·Pin 같은 동적 instrumentation 도구와 달리
**실행 바이너리 없이** 정적으로 캐시 동작을 예측한다.

---

## 메모리 모델 — Miss Upper-Bound

정적 도구는 실제 실행 주소를 알 수 없다. 별도로 할당된 객체(배열·구조체)들의
상호 주소 관계는 컴파일러의 섹션 배정·재배열, allocator 동작, ASLR에 따라
빌드/실행마다 달라지므로 소스나 IR만으로는 **불가지(不可知)**하다.

APEX-Cache는 이 불확정성을 **보수적 상한(upper bound)** 방향으로 고정한다.

- **객체 base를 캐시 라인 크기로 정렬한다.** 이는 "서로 다른 객체는 같은 캐시
  라인을 공유하지 않는다"고 가정하는 것이다. 두 객체가 실제로 인접 배치되어
  한 라인을 공유했다면 두 번째 접근이 hit이 됐겠지만, 링커가 떼어놓았다면
  miss다. 후자를 가정하면 **miss를 과소계상하지 않는** 안전한 상한이 된다.
- 따라서 보고되는 miss 수는 실제 실행의 **상한**이며, 실제 프로그램은
  이보다 적거나 같은 miss를 낸다.

### 모델이 정확한 영역과 가정인 영역

| 단위 | 배치 | 근거 |
|------|------|------|
| 배열 **내부** 원소 | `base + i*elem_size` 연속 | 언어가 보장 (정확) |
| 구조체 **내부** 멤버 | `base + member_offset` 연속 | 언어가 보장 (정확) |
| 별도 객체 **사이** | 각 base를 라인 정렬 | 불가지 → 보수적 상한 (가정) |

### 전제 조건

상한 성질은 **객체 정렬 단위 = 캐시 `line_size`** 일 때 성립한다. 정렬이
line_size보다 작으면 두 객체가 한 라인을 공유할 수 있어 상한이 깨진다.
실제 파이프라인은 `cache.yaml`의 `line_size`를 `MemoryLayout`에 주입하여
이 조건을 보장한다. (`MemoryLayout`의 기본 정렬 32는 인자 미지정 시의
fallback이며, 정식 실행 경로에서는 항상 line_size가 주입된다.)

---

## 기능

- AP JSON → AccessEvent 선형 스트림 복원 (루프 언롤, 함수 call expansion)
- YAML 기반 캐시 계층 설정 (`cache.yaml`) — private L1 × N + shared L2 + Memory
- LRU replacement, write-back / write-through 정책
- cold / capacity / conflict miss 분류
- **귀속 단위:** benchmark / function · core · region · loop · memory object · access site · load/store
- inclusive / exclusive Region 집계
- rule-based 최적화 진단 (padding, blocking, loop interchange 힌트)
- 출력: `summary.csv`, `summary.json`, `diagnostics.md`, 계층별 breakdown CSV

---

## 빌드

### 요구 사항

| 도구 | 최소 버전 |
|------|-----------|
| CMake | 3.20 |
| GCC / Clang | C++17 이상 |
| yaml-cpp | 0.7 |
| nlohmann/json | 3.10 |
| GTest | 1.11 |

Ubuntu 22.04 기준 의존성 설치:

```bash
sudo apt-get install -y \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libgtest-dev
```

### 빌드 및 테스트

```bash
# 서브모듈 초기화
git submodule update --init --recursive

# 빌드
cmake -S . -B build
cmake --build build

# 테스트 (GTest 바이너리 직접 실행)
./build/test_ap_loader
./build/test_event_builder
./build/test_memory_layout
./build/test_address_mapper
./build/test_cache_set
./build/test_cache_level
./build/test_cache_hierarchy
./build/test_yaml_parser
```

---

## 사용법

```bash
# 단일 실행 (구현 완료)
apex-cache run input.ap.json --cache cache.yaml [--shapes shapes.yaml] [--output results/]

# sweep: L1 용량 범위 탐색 (예정)
apex-cache sweep input.ap.json --cache cache.yaml --l1-sizes 4K,8K,16K,32K
```

`run`은 `--output`(기본 `results/`)에 `summary.csv`, `summary.json`,
`diagnostics.md`, `object_breakdown.csv`를 생성한다.

### 설정 파일

**cache.yaml** — 캐시 계층 정의

```yaml
cores:
  count: 4
  mapping:
    - id: 0
      l1: L1D0

caches:
  - name: L1D0
    role: L1
    private_to: 0
    size_bytes: 32768
    line_size: 64
    associativity: 8
    replacement: LRU
    write_policy: write-back
    write_allocate: true
    delay_cycles: 4
    next: L2
  - name: L2
    role: LLC
    size_bytes: 262144
    line_size: 64
    associativity: 8
    replacement: LRU
    write_policy: write-back
    write_allocate: true
    delay_cycles: 12
    next: Memory

memory:
  name: Memory
  delay_cycles: 120
```

**shapes.yaml** — AP JSON에 shape가 없는 배열 보완

```yaml
shapes:
  A: [1000, 1000]
  B: [1000, 1000]
element_size: 4
object_alignment: 64
```

**mapping.yaml** — SMP 함수→core 정적 매핑 (선택)

```yaml
mappings:
  - function: kernel_2mm
    core: 0
  - function: init_tmp
    core: 1
```

---

## 출력 예시 (구현 예정)

**summary 표**

```
Benchmark  L1 Miss%  L2 Miss%  Mem Access  AMAT(cyc)  Top Object  Top Loop
2mm        8.31      1.42      1240        7.18       tmp         i-j-k
atax       4.18      0.77       391        5.02       A           i-j
```

**diagnostics.md**

```
Rank  Loop    Access       Miss%  Share  Miss Type   Hint
1     i-j-k   B[k][j]      31.2   44.8   conflict    loop interchange 검토
2     i-j-k   tmp[i][j]     9.7   18.1   store       write allocation 비용 확인
3     i-j     A[i][j]       6.4   12.3   capacity    blocking 검토
```

---

## 구현 현황

| Phase | 내용 | 상태 |
|-------|------|------|
| 1 | CMake 골격 + 디렉터리 구조 | ✅ 완료 |
| 2 | AP Layer (ApLoader, EventBuilder) | ✅ 완료 |
| 3 | Memory Layer (MemoryLayout, AddressMapper) | ✅ 완료 |
| 4 | Cache Layer (YamlConfigParser, LRU 시뮬레이션) | ✅ 완료 |
| 5 | Analysis Layer (Attribution, MissClassifier, Diagnostics) | ✅ 완료 |
| 6 | Report Layer (CSV / JSON / Markdown) | ✅ 완료 |
| 7 | CLI (`run`) + 통합 테스트 | ✅ 완료 |
| 8 | Python 후처리 scripts | ⬜ 예정 |

### 알려진 제약 (Known Limitations)

| 항목 | 현황 |
|------|------|
| `sweep` 모드 | 미구현 — `run`만 제공 |
| AMAT / delay 출력 | 미구현 — 현재 출력은 miss 개수만. `cache.yaml`의 `delay_cycles`는 시뮬레이션에 쓰이나 리포트에 노출 안 됨 |
| 함수 간 call 전개 | Pipeline 미지원 (top-level 노드만). region inclusive/exclusive는 Attribution 단위 테스트로만 검증 |
| 멀티코어 | 설정은 가능하나 통합 검증은 단일 코어 기준 |
| **AP 입력 v2 전환** | **진행 중.** APE가 access_path 기반 v2(`*_g_ape.json`)를 출력하나 현재 ApLoader는 구 형식만 파싱 → `run examples/*.json`은 아직 실패. v2 전용 마이그레이션 작업 중 (구조체 `metadata.structs` 포함) |

---

## 저장소 구조

```
APEX-Cache/
  CMakeLists.txt
  frontend/                    ← Access-Pattern-Extractor 서브모듈
  include/
    ap/                        ← ApNode, ApLoader, EventBuilder, AccessEvent
    memory/                    ← MemoryLayout, AddressMapper
    cache/                     ← CacheConfig, YamlConfigParser, CacheHierarchy
    analysis/                  ← Attribution, MissClassifier, Diagnostics
    report/                    ← CsvWriter, JsonWriter, MarkdownWriter
  src/                         ← 각 Layer 구현
  tests/                       ← GTest 단위 테스트
  examples/                    ← APE LAT v2 입력 샘플 (*_g_ape.json)
  settings/                    ← cache.yaml 등 실행 설정
  scripts/                     ← Python 후처리 (Phase 8)
  results/                     ← 출력 디렉터리 (gitignore)
```

---

## 라이선스

TBD