# 로컬라이제이션 / 문자열 테이블 설계 (Localization & String Table)

UTF-8 문자열 테이블 + 언어 설정 + 언어별 텍스트 조회 구조.
**상태: §10-1·2·3 구현 + §10-5 부분. 언어 설정이 저장·복원되고 `Application`이 시작 시·변경 시 해당 언어
표를 읽는다. 다만 `assets/loc/*.txt`가 아직 없고 어떤 위젯 라벨도 표에서 오지 않아 화면 텍스트는 바뀌지
않는다 — 문자열 이관(§10-4)이 다음 단계.**

기존 선례를 그대로 따른다 — 파일 포맷은 `core::Settings`(`settings.cfg`)의 `key=value` 계열,
언어 선택은 해상도/프레임레이트와 같은 **고정 후보 목록 + 인덱스** 방식(`kResolutionPresets` 패턴).

관련: `docs/game-settings.md`(설정 추가 절차 — §4가 그대로 따름), `docs/ui-architecture.md`(화면 전환 =
`SetScreen` 통째 교체 — §5의 근거), `docs/texture-atlas-and-sprite-pass.md` §1.3(`GlyphAtlas` — 한글 **표시**의
선행 조건, §8), `docs/atlas-build-pipeline.md`(글리프 서브셋을 굽는 곳), `docs/save-load-design.md`(설정 ≠ 진행도
분리 원칙 동일).

---

## 0. 범위와 전제

| | |
|---|---|
| **이 문서** | 문자열을 키로 찾아 **현재 언어의 UTF-8 문자열**을 돌려주는 구조 + 언어 설정 + 전환 흐름 |
| **이 문서 아님** | 글리프 래스터라이즈·폰트 아틀라스(§8에서 의존만 명시), 문자열 번역 자체, IME·텍스트 입력 |

**중요한 전제 — 표는 지금 깔 수 있지만, 한글은 아직 화면에 안 나온다.**
현재 텍스트 렌더는 `UI.cpp`의 5×7 절차 폰트이고 `AddText`가 **바이트 단위**로 순회하며 A-Z·0-9·`: - . %`만
그린다. 즉 `ko.txt`를 읽어 `TextLine`에 넣어도 **빈 글리프**가 나간다.

그래도 이 구조를 먼저 만드는 이유는 두 가지다:
1. 하드코딩된 영어 문자열을 **지금** 걷어내지 않으면, 나중에 화면이 늘어난 뒤 전수 조사해야 한다.
2. **문자열 표가 곧 글리프 서브셋의 목록**이 된다(§8) — 폰트 작업의 입력이 이 표다.

---

## 1. 계층 — 2개 타입 + 조율자

```
assets/loc/en.txt ─┐
assets/loc/ko.txt ─┴──►  core::StringTable      한 언어의 key → UTF-8 value (불변, 조회만)
                              ▲
        core::Localization ───┘   활성 언어 + 폴백 사슬 + 재로드  (어떤 표를 쓸지 결정)
                              ▲
        game::Application ────┘   Settings.languageIndex 를 읽어 Load, 언어 변경 시 화면 재빌드
                              │
        game/*Screen.cpp  ◄───┘   const Localization& 을 받아 키를 해석해 위젯에 완성된 문자열 전달
```

| 타입 | 책임 (하나) | 아닌 것 |
|---|---|---|
| `core::StringTable` | 파일 하나를 파싱해 `key → value` 보관, `Find(key)` | 언어 정책·폴백·파일 선택 |
| `core::Localization` | 활성 언어 보관, 폴백(요청→기본→키), `Get`/`Format` 진입점, 언어 전환 시 재로드 | 파싱 세부·UI |
| `game::Application` | 설정에서 언어를 읽어 로드하고, 바뀌면 현재 화면을 다시 만든다 | 문자열 내용 |

### `ui` 모듈은 로컬라이제이션을 모른다 (ISP/DIP)

위젯(`Button`/`TextLine`)은 지금처럼 **완성된 `std::string`만** 받는다. 키 해석은 화면을 만드는
자유 함수(`BuildTitleScreen` 등, `game/`)가 한다.

- 위젯에 `const Localization&`를 주입하면 UI 전체가 로컬라이제이션에 의존하게 된다 — `UIRenderer`를
  얇게 유지하려는 `ui-architecture.md`의 방향과 정반대.
- 화면은 어차피 `UIContext::SetScreen`으로 **통째 교체**되므로, 언어가 바뀌면 다시 만들면 된다(§5).
  즉 "런타임에 라벨을 갱신하는 장치"가 필요 없다 — 이게 이 설계에서 가장 큰 단순화다.

---

## 2. 키 규약

**판단 A/B (권장: A)**

- **A — 안정 키**(`ui.title.start`): 모든 언어가 데이터. 영어 문구를 고쳐도 다른 언어가 안 깨진다.
  누락 시 키 자체가 화면에 보여(`ui.title.start`) 바로 눈에 띈다.
- **B — 원문(영어)을 키로**: 키 표를 따로 관리 안 해도 되지만, 영어 한 글자만 고쳐도 모든 번역이 끊긴다.

**규약**: 소문자 + 점 구분 `<영역>.<화면/맥락>.<이름>`.

```
ui.title.start            ui.settings.language       hud.wave.label
ui.title.settings         ui.settings.resolution     hud.phase.combat
ui.common.back            ui.settings.master_volume  hud.phase.prep
```

- 영역: `ui.` (버튼·라벨) / `hud.` (인게임 표시) / `msg.` (알림·오류).
- **디버그·개발용 텍스트는 로컬라이즈하지 않는다** — FPS 카운터, 디버그 오버레이는 영문 ASCII 고정
  (번역 비용만 들고 폰트 서브셋만 키운다).

---

## 3. 파일 포맷 — UTF-8 `key=value`

언어당 파일 하나. `Settings`의 파서와 같은 계열이라 새 의존성이 없다.

```
assets/loc/en.txt
assets/loc/ko.txt
```

```
# assets/loc/ko.txt  — '#'로 시작하는 줄은 주석
ui.title.start=시작
ui.title.settings=설정
ui.common.back=뒤로
hud.wave.label=웨이브 {0}
msg.save.failed=저장에 실패했습니다.\n디스크 공간을 확인하세요.
```

| 규칙 | |
|---|---|
| 인코딩 | **UTF-8**. BOM이 있으면 첫 줄에서 제거하고 계속 읽는다 |
| 파싱 | 첫 `=`에서 한 번만 분리. 값 안의 `=`는 그대로 값의 일부 |
| 공백 | 키는 트림, **값은 트림하지 않는다**(의도적 공백 보존) |
| 주석/빈 줄 | `#`로 시작하는 줄과 빈 줄은 무시 |
| 이스케이프 | `\n`(줄바꿈), `\\`(역슬래시) 둘만. 그 외 `\x`는 그대로 둔다 |
| 중복 키 | 나중 것이 이긴다(오버라이드 파일을 얹을 때 필요한 규칙). **로그는 없다** — `core`에 로깅 설비가 없어 `StringTable`이 Windows 헤더를 끌어오지 않게 두었다. 누락·중복 보고는 미스 카운터와 함께 `Localization`이 맡는다(§6) |
| 파서 관점 | **바이트 단위**로 충분하다 — UTF-8은 ASCII와 충돌하지 않으므로 `=`·`\n`·`#` 판정이 멀티바이트 문자를 자르지 않는다. 디코딩은 렌더 단계(§8)의 일 |

파일 경로는 ASCII(`assets/loc/ko.txt`)로 고정한다 — Windows narrow 파일 API의 코드페이지 문제(한글 경로)를
이 시스템이 건드리지 않게 하기 위함.

---

## 4. 언어 설정 (`core::Settings` 확장)

`docs/game-settings.md` §3의 "새 설정 항목 추가하기" 절차를 그대로 따른다. 해상도/프레임레이트와 같은
**고정 후보 목록 + 인덱스** 형태다.

```cpp
// core/Settings.h  — kResolutionPresets 옆
struct Language { const char* code; const char* nativeName; };

inline constexpr std::array<Language, 2> kLanguagePresets{ {
    { "en", "ENGLISH" },     // nativeName 은 폰트가 감당할 때까지 ASCII 로
    { "ko", "KOREAN" },      // GlyphAtlas 이후 "한국어" 로 교체 (§8)
} };

struct Settings
{
    // ...
    int languageIndex{ 0 };   // kLanguagePresets 인덱스. 범위 밖이면 LoadOrDefault 가 0 으로 클램프
};
```

- `LoadOrDefault`/`Save`에 `language=` 줄 추가(다른 필드와 동일).
- **인덱스가 아니라 코드를 저장할지**: 파일에는 `language=ko`처럼 **코드 문자열**로 쓰는 쪽을 권장한다 —
  프리셋 배열 순서가 바뀌어도 사용자 설정이 엉뚱한 언어로 밀리지 않는다(해상도는 값 자체가 배열에
  적혀 있어 문제가 없지만, 언어는 목록이 계속 늘어난다). 로드 시 코드→인덱스로 변환, 모르는 코드는 기본값.
- 설정 UI는 해상도와 같은 **사이클 행**(◀/▶로 `((i + step) % count + count) % count`)을 재사용한다.

---

## 5. 언어 전환 흐름

```
사용자가 설정에서 언어 변경
  └─ settings.languageIndex 갱신 (즉시, 메모리)
  └─ Application::ApplyLanguage()
       ├─ m_localization.Load(code)                 // 표 교체 (작은 파일, 동기)
       ├─ m_ui.SetScreen(BuildXxxScreen(...))       // 현재 화면 재빌드
       └─ m_ui.SetOverlay(BuildSettingsScreen(...)) // 열려 있던 오버레이도 다시 (포커스/스크롤은 초기화됨)
  └─ 설정 오버레이를 닫을 때 Settings::Save (기존 정책 그대로)
```

- vsync·해상도가 "즉시 적용"인 것과 같은 부류로 취급한다(`game-settings.md` "실제 부작용이 있는 필드").
- **재빌드로 반영**하므로 위젯에 새 기능이 필요 없다(§1).

> ⚠ **선행 조건 — `UIContext`의 지연 교체.** 언어 변경은 설정 화면의 PREV/NEXT **버튼 콜백 안에서**
> 일어나는데, `SetScreen`/`SetOverlay`/`ClearOverlay`는 지연 큐 없이 **옛 트리를 그 자리에서 파괴**한다
> (`ui-architecture.md` "화면 교체 시점"). 즉 재빌드를 그대로 넣으면 **실행 중인 버튼의 `onClick`
> (`std::function`)을 그 안에서 파괴**하게 된다 — 지금 `CLOSE` 버튼이 이미 하고 있는 UB를 한 건 더
> 늘리는 것이다. 그래서 §10-5에서 표 재로드만 넣고 **재빌드는 보류**했다.
> 순서: `UIContext`에 `m_pendingScreen`/`m_pendingOverlay` 지연 슬롯을 먼저 넣고, 그다음 재빌드.
- 인게임 HUD 텍스트(`SnapshotBuilder`가 `ui::DrawText`로 직접 그리는 웨이브/페이즈/FPS)는 매 프레임
  새로 만들어지므로 자동으로 다음 프레임부터 새 언어가 된다.

---

## 6. 폴백과 누락 정책

조회 순서: **활성 언어 → 기본 언어(`en`) → 키 문자열 그대로**.

| 상황 | 결과 |
|---|---|
| 활성 언어에 키 있음 | 그 값 |
| 활성 언어에 없고 기본 언어에 있음 | 기본 언어 값 (번역 미완 화면도 읽을 수는 있음) |
| 어디에도 없음 | **키 자체를 반환**(`ui.title.start`) — 빈 문자열이나 크래시보다 낫다. 화면에서 바로 보인다 |
| 언어 파일 자체가 없음/깨짐 | 기본 언어로 폴백하고 **시작은 막지 않는다**(`Settings::LoadOrDefault`와 같은 철학) |

- 디버그 빌드에서 **미스 키를 집계**해 종료 시 로그로 덤프하면 번역 누락이 조용히 쌓이지 않는다.
- `Get`은 `std::string_view`를 돌려준다 — 표가 살아 있는 동안 유효(표는 언어 전환 시에만 교체되고,
  그 시점은 프레임 경계 + 화면 재빌드와 같이 일어난다). **`string_view`를 프레임 너머로 보관하지 않는다.**

---

## 7. 서식화 — `{0}` 치환, 문자열 연결 금지

```cpp
loc.Format("hud.wave.label", 12);      // ko: "웨이브 12"   en: "WAVE 12"
```

- 자리표시자는 `{0}`, `{1}`... **번호 기반**이어야 한다. 언어마다 어순이 다르므로 "앞에서부터 순서대로"가
  성립하지 않는다(`ko`: "적 5마리 남음" ↔ `en`: "5 ENEMIES LEFT" — 숫자 위치가 다르다).
- **하지 말 것**: `Get("hud.wave") + " " + std::to_string(n)` 같은 조각 연결. 이게 로컬라이제이션에서
  가장 흔한 되돌리기 어려운 실수다.
- **복수형(plural)은 v1 범위 밖**이다. 한국어는 복수 변화가 없고 영어만 필요한데, 필요해지면 그때
  `hud.enemy.one` / `hud.enemy.many` 두 키로 처리한다 — ICU식 규칙 엔진을 미리 만들지 않는다(YAGNI).

---

## 8. 폰트/글리프 의존 — 표가 서브셋의 단일 소스

한글을 **표시**하려면 `GlyphAtlas`(`texture-atlas-and-sprite-pass.md` §1.3)와 UTF-8 디코딩이 필요하다.
지금 `AddText`는 바이트를 순회하므로, 그 단계에서 **코드포인트 순회**로 바뀌어야 한다.

```cpp
// 지금: 바이트 = 글리프 (ASCII 전용)
for (const char character : text) { ... }

// GlyphAtlas 이후: 코드포인트 = 글리프
for (const char32_t cp : Utf8Range(text)) { const GlyphMetrics* g = atlas.Glyph(cp); ... }
```

**규모 문제와 그 해법**: 한글 완성형은 11,172자라 전부 구우면 아틀라스가 감당이 안 된다. 해법은
**실제로 쓰는 글자만 굽는 것**이고, 그 목록은 정확히 **이 문자열 표**다.

- `tools/atlas_pack`에 "`assets/loc/*.txt`를 스캔해 등장하는 코드포인트 집합만 래스터라이즈" 단계를 추가한다.
- 결과: 표에 없는 글자는 굽지 않는다 → 아틀라스가 작게 유지된다. 표에 문장을 추가하면 다음 아틀라스
  빌드에 자동으로 포함된다.
- 런타임 동적 아틀라스(입력창처럼 임의 문자가 들어오는 경우)는 그 기능이 실제로 생길 때 별도 결정.

즉 **이 문서의 구조를 먼저 만들면 폰트 작업의 입력이 자동으로 준비된다** — 순서가 이쪽이 먼저인 이유.

---

## 9. 사용 방법 (How to use)

> 구현 시점의 API 초안이다(현재 미구현).

### 새 문자열 추가하기

1. `assets/loc/en.txt`에 `키=영문` 추가(기본 언어가 먼저 — 폴백의 기준).
2. 다른 언어 파일에 같은 키 추가. 빠뜨려도 영어로 폴백되고 디버그 로그에 남는다(§6).
3. 코드에서는 **키로만** 부른다.

### 화면에서 쓰기

```cpp
// game/TitleScreen.cpp - 화면 빌더가 키를 해석해 완성된 문자열을 넘긴다
std::unique_ptr<ui::Widget> BuildTitleScreen(const core::Localization& loc,
                                             std::function<void()> onStart, /* ... */)
{
    auto start = std::make_unique<ui::Button>(std::string(loc.Get("ui.title.start")));
    // ... 위젯은 로컬라이제이션을 모른다
}

// HUD (SnapshotBuilder, 매 프레임)
ui::DrawText(out, loc.Format("hud.wave.label", waveIndex), { 16, 16 }, 2.0f, kHudColor);
```

### 새 언어 추가하기

1. `assets/loc/<code>.txt` 추가(`en.txt`를 복사해 값만 번역).
2. `core::kLanguagePresets`에 `{ "<code>", "<표시명>" }` 한 줄 추가.
3. 폰트 아틀라스를 다시 굽는다(§8 — 새 글자가 자동으로 서브셋에 들어온다).

코드 변경은 2번 한 줄뿐이다. 그게 이 구조의 목표다(OCP).

### 하지 말 것

- 위젯(`ui::Button`/`TextLine`)에 `Localization`을 주입하지 않는다 — 키 해석은 화면 빌더에서(§1).
- 번역 조각을 **문자열 연결로 조립하지 않는다** — `Format`의 `{0}`을 쓴다(§7).
- 사용자에게 보이는 문자열을 코드에 하드코딩하지 않는다. 반대로 **디버그/개발용 텍스트는 표에 넣지 않는다**(§2).
- `Get`이 돌려준 `string_view`를 프레임 너머로 보관하지 않는다(§6).
- 렌더 스레드·`JobSystem` 워커에서 조회하지 않는다 — 조회는 메인 스레드(화면 빌드·스냅샷 빌드)에서만.
  스냅샷에는 이미 해석된 결과(`Quad`/`SpriteDraw`)만 실린다(불변 규칙 3).
- 언어 파일이 없다고 **시작을 막지 않는다**(§6).
- 진행 상태(세이브)에 번역된 문자열을 저장하지 않는다 — 키를 저장하고 표시할 때 해석한다
  (`save-load-design.md` §1의 "파생은 저장하지 않는다"와 같은 이유).

---

## 10. 구현 순서

1. ~~`core/StringTable.{h,cpp}` — UTF-8 `key=value` 파서(BOM·주석·`\n` 이스케이프) + `Find`~~ **✅ 구현**.
   `LoadFromFile`(열기 실패 시에만 `false`, 깨진 줄은 건너뜀) + `Find(string_view) -> const std::string*`
   (`detail::StringViewHash` 투명 해시라 프레임마다 조회해도 문자열을 새로 만들지 않음) + `Size`/`Empty`.
   BOM 제거·CRLF·첫 `=` 분리·값 공백 보존·`\n`/`\\` 이스케이프·중복 후승을 모두 처리한다.
2. ~~`core/Localization.{h,cpp}` — 활성/기본 표 2개 보유, `Get`/`Format`, `Load(code)`, 미스 카운터~~ **✅ 구현**.
   `Load(code)`는 기본 언어 표를 한 번만 읽어 폴백으로 보관하고, 요청 언어 파일을 못 읽으면 `false`를
   돌려주되 **조회는 계속 기본 언어로 해결**된다. 활성 언어가 곧 기본 언어면 활성 표를 비워 두 번 읽지 않는다.
   `Format`은 `std::format`이 아니라 자체 `{N}` 치환 — 패턴이 **데이터 파일에서 오므로** 번역자의 오타(`{5}`,
   짝 없는 `{`)에 예외를 던지면 안 되고, 그런 조각은 **그대로 화면에 보이게** 남긴다. 언어 코드는
   `[A-Za-z0-9_-]`만 허용(설정 파일이 손편집 가능한 경계라 경로 형태의 코드를 파일명으로 쓰지 않기 위함).
   미스 키는 `MissedKeys()`에 distinct로 쌓이고 `Load` 시 초기화된다.
3. ~~`core::Settings`에 `languageIndex` + `language=` 줄(§4) + `kLanguagePresets`~~ **✅ 구현**.
   파일엔 코드로 저장(`language=ko`)하고 모르는 코드는 기본값으로 되돌린다. `Settings::LanguageCode()`가
   `Localization::Load`에 넘길 코드를 준다. `Application`이 `m_localization`을 소유하고 시작 시 +
   설정 변경 시 `ApplyLanguage()`로 표를 읽는다. 설정 화면엔 해상도와 같은 PREV/NEXT 사이클 행 추가.
   **화면 텍스트는 아직 안 바뀐다** — 라벨이 표에서 오지 않기 때문(4·5번).
4. `assets/loc/en.txt` 생성 — **지금 하드코딩된 UI 문자열을 전부 여기로 이관**(가장 품이 드는 단계).
   화면 빌더 시그니처에 `const core::Localization&` 추가.
5. **부분 ✅** — 언어 사이클 행 + `Application::ApplyLanguage`(표 재로드)까지 됨. **남은 것: 화면 재빌드**
   (`SetScreen`/`SetOverlay`, §5). 이유는 두 가지 — 4번으로 라벨이 표에서 와야 의미가 생기고, 그 전에
   `UIContext` 지연 교체가 필요하다(§5의 ⚠ 박스). 순서: 지연 교체 → 문자열 이관 → 재빌드.
6. `assets/loc/ko.txt` — 데이터만 준비(표시는 §8 이후).
7. (§8) `GlyphAtlas` + `AddText` 코드포인트 순회 + `atlas_pack`의 loc 스캔 서브셋.

1~5가 이 문서의 "구조"다. 6~7은 폰트 작업과 묶여 별도 착수.

---

## 11. 판단 필요

1. **키 규약 A(안정 키) / B(원문 키)** — §2. 권장 A.
2. **설정 파일에 인덱스 vs 언어 코드** — §4. 권장 코드(`language=ko`).
3. **첫 실행 시 OS 언어 자동 감지 여부**(`GetUserDefaultUILanguage`) — 감지하면 한국 사용자가 바로
   한국어를 보지만, 폰트가 준비되기 전엔 빈 화면이 된다. **폰트 이후로 미루는 쪽**을 권장.
4. **지원 언어 목록** — 한/영 2개로 시작할지, 더 늘릴지(번역 유지 비용이 선형으로 는다).
5. **`nativeName` 표기**(설정 화면의 언어 이름) — 폰트 전엔 ASCII(`KOREAN`), 이후 `한국어`로 교체하는
   2단계를 받아들일지.
