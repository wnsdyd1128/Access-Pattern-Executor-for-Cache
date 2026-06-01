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
```

---

## 사용법 (구현 예정)

```bash
# 단일 실행
apex-cache run input.ap.json --cache cache.yaml [--shapes shapes.yaml] [--output results/]

# sweep: L1 용량 범위 탐색
apex-cache sweep input.ap.json --cache cache.yaml --l1-sizes 4K,8K,16K,32K
```

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
| 3 | Memory Layer (MemoryLayout, AddressMapper) | 🔧 진행 중 |
| 4 | Cache Layer (YamlConfigParser, LRU 시뮬레이션) | ⬜ 예정 |
| 5 | Analysis Layer (Attribution, MissClassifier, Diagnostics) | ⬜ 예정 |
| 6 | Report Layer (CSV / JSON / Markdown) | ⬜ 예정 |
| 7 | CLI + 통합 테스트 | ⬜ 예정 |
| 8 | Python 후처리 scripts | ⬜ 예정 |

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
  scripts/                     ← Python 후처리 (Phase 8)
  results/                     ← 출력 디렉터리 (gitignore)
```

---

## 라이선스

TBD