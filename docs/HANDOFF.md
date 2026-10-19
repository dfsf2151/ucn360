# Handoff — نقل UCN (HTML5) إلى Xbox 360 كـ XEX

- **التاريخ:** 2026-10-19 (آخر جلسة حتى ~00:10)
- **المشروع:** `/home/mohamd/Projects/UCN_MVP_Xbox360` — الفرع **`m3/event-interpreter`** (main لم يُلمس منذ `2236d30`)
- **المخرَج النهائي:** `default.xex` يشغّل **لعبة UCN نفسها** من Aurora، مع صوت وحفظ و60fps.
- **المرجع الحيّ:** `web_probe/` (خادم :8765 + Chrome headless بـCDP :9222). كل مقارنة حالة أو بكسل تكون ضدّه عبر **الأوراكل الحتمي** (`tools/cdp_tick_oracle.js`).
- **توجيه المستخدم:** بعد M3 **لا مراحل منفصلة**. ابنِ XEX اللعبة الكاملة بدفعة واحدة متواصلة: إدخال، تنقل إطارات، xenon، أصول، صوت، حفظ، كل الإطارات، 60fps. سلّم أول XEX قابل للتجربة بأسرع ما يمكن.

---

## 1) أين وصلنا

| المرحلة | الحالة | الدليل |
|---|---|---|
| M0 منصة | ✅ على الجهاز (بناء قديم)؛ بناء `xsdk_present_*` الجديد **لم يُختبر على الجهاز** | `dist/ucn360-m0.zip` محدَّث |
| M1 + M2 | ✅ مُودَعان (`38a01d1`, `2236d30`) | إطار 6 مطابق بالبكسل |
| **M3 آلة الأحداث** | ✅ **شبه مكتمل** | التفاصيل أدناه |
| XEX كامل | ⏳ بدأ التحضير: حزمة صور DXT + حزمة صوت جاهزتان | `build/xenon/images.pak`, `sounds.pak` |

### M3: النتيجة الفعلية
محرك C++ (`engine/game/`) ينفّذ برنامج الأحداث بمنطق `Runtime.js`. يقارن `tools/m3_parity.sh` كل إطار مع تسجيله الحيّ (2000 tick، seed 12345، بلا إدخال):

| الإطار | النتيجة |
|---|---|
| 0, 1, 2, 4, 6, 7, 10, 11, 12, 15 | ✅ 2000 tick متطابقة (الموضع، الصورة، الأنيميشن، الإطار، الاتجاه، أعلام الرسم، القيم، العدادات، النص، المؤقت، حالة الـLCG) |
| 3 / 5 / 9 / 14 | ✅ متطابقة حتى خروج اللعبة من الإطار (tick 119 / 765 / 1284 / 807)، والطرفان يخرجان في نفس الـtick |
| 8 | ✅ 2000 tick (التمرير "center display" منقول ومُختبر) |
| 13 | ⚠️ لا تسجيل ممكن: يغادر في أول tick قبل أن يراه الأوراكل محمَّلًا. طبقاته في `layers.json` مستنتجة |

**البكسل:** `frame_render --ticks=300` يرسم حالة العالم (`engine/render/world.cpp`). القائمة (إطار 1) مقابل لقطة اللعبة عند نفس الـtick: **mean 0.149، بكسلات قوية 0.01%**، كلها نص الإصدار "1.031" (النصوص بخط غير مرسومة بعد). الباقي فروق تقريب ±1.

### حزم الجهاز (جاهزة، غير مُختبرة على الجهاز)
- `platform/host/asset_pack` → `build/xenon/images.pak`: **152MB**، 2100 صورة بمقياس 2/3 (منطق 1920×1080 يُرسم على 1280×720)، DXT1 (142) / DXT5 (1958). **تتسع كلها في الذاكرة**، فلا حاجة لـLRU.
- `tools/sound_pack.py` → `build/xenon/sounds.pak`: **220MB** PCM16 mono 22050Hz، 308 عينة (85 دقيقة). **يُبثّ من القرص**، لا يُحمَّل كاملًا.

---

## 2) ⚠️ حالة غير مستقرة الآن (أصلحها أولًا)

1. **`tools/compile_bundle.py` سيفشل** لأن `w_framert` يقرأ `assets_raw/dump/oracle/layers.json` غير الموجود بعد. القيم للإطارات 0–12 محفوظة في `assets_raw/dump/oracle/layers_partial_0_12.json`. الإطارات 13–15 لم تُفرَّغ (رفض المستخدم الأمر الطويل؛ شغّله بالخلفية).
2. **`build/bundle/bundle.bin` الحالي قديم** (بلا طبقات)، و`game_host` الجديد **ينهار عليه** (`Defs::load` يقرأ طبقات غير موجودة). أعد بناء الحزمة بعد T1.
3. **قسم Results في `docs/superpowers/plans/2026-10-18-m3-event-interpreter.md` يبالغ:** يقول إن كل الإطارات تطابق. صحّحه للإطارين 8 و13 حسب نتيجة T1–T2.
4. تغييرات كثيرة **غير مُودَعة** (القسم 5).

---

## 3) المهام التالية بالترتيب

- **T1:** أكمل `layers.json`:
  ```bash
  for f in 13 14 15; do node tools/cdp_tick_oracle.js --ticks 1 --frame $f --eval 'JSON.stringify(window.__rt.Uc.frame.pb.map(l => [l.La, l.zj, l.Bj]))' 2>/dev/null | tail -1; done
  ```
  ثم ادمج مع `layers_partial_0_12.json` في `assets_raw/dump/oracle/layers.json` (`{"0":[[La,zj,Bj],...],...}`)، أو شغّل `tools/dump_layers.sh` كاملًا في الخلفية. بعدها `python3 tools/compile_bundle.py`.
- **T2:** `tools/m3_parity.sh` → أصلح الإطار 8 (التمرير)، وسجّل الإطار 13 (`node tools/cdp_tick_oracle.js --ticks 2000 --frame 13 --out build/oracle/frame_13.jsonl`) وقارنه. صحّح Results ثم commit.
- **T3 — XEX كامل** (`platform/xenon/game/`), بدفعة واحدة:
  1. بناء `engine/` بـ`powerpc-xsdk-elf-g++` (`-lstdc++` مدعوم). القراءة صارت little-endian صريحة في `Bundle::read`. انتبه لأي `memcpy` من بيانات الحزمة.
  2. ملفات: `game:\bundle.bin`، `game:\images.pak`، `game:\sounds.pak` عبر `open/read` POSIX في xsdk (`src/file.c`).
  3. العرض على GPU: نموذج كواد مُنسَّج من `third_party/xsdk/tests/test_texture.c` + `texture_shaders.h`. ارفع كل صور DXT عند الإقلاع (`xsdk_texture_create` + `xsdk_texture_upload`، الصيغة `XSDK_FORMAT_DXT1/DXT5`). ارسم بترتيب `world.cpp`: الطبقات تصاعديًا، وفي كل طبقة gc ثم Ib ثم Pa. ألفا = قواعد `ink_alpha`. الإحداثيات ×2/3 إلى 1280×720، وأضف إزاحة التوسيط للإطارات 800×480. العرض عبر `xsdk_present_*`.
  4. الإدخال: `XamInputGetState`. مؤشر يتحرك بالعصا يغذّي `Run::set_mouse` (نافذة 1920×1080). زر A = نقر. يلزم نقل شروط النقر `(-6,-7)`, `(-6,-5)` و`po` (6551) و`keys_`.
  5. تنقّل الإطارات في `Run`: عند `zb` بعد `tick`: 1 = التالي، 2 = السابق، 3 = `Dm & 0x8000 ? index : xn[Dm]` حيث `xn = [1,2,3,4,5,6,7,9,0,8,11,12,10,13,14,15]` (`assets_raw/dump/oracle/app.json`)، ثم `start_frame` (الـIni يبقى، الصوت يتوقف).
  6. الصوت: خلاط فوق حالة القنوات الموجودة (`Run::channel_`, `samples_`) يقرأ `sounds.pak`، وإعادة معايرة 22050→48000، ومخرج `XAudioRegisterRenderDriverClient` (مثال: `third_party/xsdk/tests/audio_check.c`).
  7. الحفظ: خريطة `Run::ini` ↔ `game:\ucn_save.dat`.
  8. النصوص (كائنات type 3 وعدادات type 5): خط نقطي مدمج أو أطلس مولَّد على host.

---

## 4) المسارات الجديدة/المهمة

| المسار | الوصف |
|---|---|
| `engine/game/run.{h,cpp}`, `run_objects.cpp`, `run_events.cpp` | المحرك: frame start، tick، الكائنات (أنيميشن، حركة ثابتة/فأرة/مسار)، الاختيار، الأحداث، التعابير، الشروط والأفعال. كل دالة تذكر سطر `Runtime.js` المقابل |
| `engine/game/defs.{h,cpp}` | قراءة OBJRT / FRAMERT / SOUNDRT / IMAGES |
| `engine/game/mask.{h,cpp}` | أقنعة التصادم (بت/بكسل، دوران/تكبير، تصادم قناع-قناع، نقطة) |
| `engine/game/value.h` | قيمة رقم/نص بدلالات JS |
| `engine/data/program.{h,cpp}` | PROGRAM v2 المُنمَّط |
| `engine/render/world.{h,cpp}` | رسم حالة العالم (المرجع لمسار GPU على الجهاز) |
| `engine/assets/dxt.{h,cpp}` | ترميز/فك DXT1/DXT5 |
| `platform/host/game_host/` | `game_host BUNDLE FRAME TICKS out.jsonl [--seed S] [--root DIR]` |
| `platform/host/frame_render/` | `... --ticks=N` يرسم العالم بعد N tick |
| `platform/host/asset_pack/` | بناء `images.pak` |
| `tools/cdp_tick_oracle.js` + `oracle_shim.js` + `oracle_capture.js` | الأوراكل الحتمي: `--ticks N --frame F --out x.jsonl --png T:file --eval EXPR` |
| `tools/compare_state.py`, `tools/m3_parity.sh` | مقارنة الحالة / فحص كل الإطارات |
| `tools/runtime_factmap.py`, `docs/runtime-map.md` | خريطة الأسماء المصغّرة ↔ المعنى |
| `tools/program_pretty.py F [--grep NAME]` | طباعة برنامج إطار مقروءًا |
| `tools/dump_sounds.sh`, `tools/dump_layers.sh`, `tools/sound_pack.py` | تفريغ مدد العينات / قيم الطبقات / حزمة الصوت |
| `assets_raw/dump/oracle/sounds.json`, `app.json` | مدد العينات + إعدادات الصوت؛ جدول `xn` |
| `build/oracle/frame_F.jsonl` | تسجيلات 2000 tick (ignored) |
| `build/ref/frame_1_t300.png` | لقطة القائمة عند tick 300 |
| `build/runtime_pretty.js` | `Runtime.js` منسَّق بـesbuild (`~/.npm-global/lib/node_modules/@shopify/cli/node_modules/@esbuild/linux-x64/bin/esbuild`) |
| `docs/superpowers/plans/2026-10-18-m3-event-interpreter.md` | خطة M3 + النتائج |

---

## 5) git

- **main repo**, الفرع `m3/event-interpreter`. آخر commits: `2c74d91` (masks، صوت، أوراكل)، `0275c4f`، `2336fdb`، `72e20a7`، `b44e8f4`.
- **غير مُودَع عند كتابة هذا:** display size (Pn/On)، الفأرة (`Eu`)، أسماء الأنيميشن البديلة (`La.ZC`)، التمرير، طبقات FRAMERT، قراءة little-endian، world renderer + العدادات، `frame_render --ticks`، إصلاحات الأوراكل (png، تفريغ طبقات العرض قبل tK، انتظار القفز)، `asset_pack` + `dxt`، `sound_pack.py`، `dump_layers.sh`، `m3_parity.sh`، Results الخطة، و`dist/ucn360-m0/README.txt` (من المستخدم سابقًا، اتركه لـT4 القديم).
- **xsdk** (`third_party/xsdk`): `4203f46`, `273d9c5` محليّان **غير مدفوعين**، والـpin في `docs/xsdk-pin.txt`.

---

## 6) دروس حرجة (أُضيفت هذه الجلسة)

- **الحتمية:** الـruntime يقرأ الوقت من `Date` فقط، ومولّده العشوائي LCG ذو 16 بت تُزرع بذرته في `Mt`. الساعة المزيفة تخطو 16/17/17ms حتى يكون `Rf = floor(t*1000/60)`. **WebAudio `onended` يجب أن يمشي على الساعة المزيفة.**
- **الوصول لإطار:** عبر مسار القفز الأصلي (`zb=3, Dm=0x8000|F`) ثم **إعادة تشغيل بـ`tK`**، مع **تفريغ حاويات الطبقات قبلها** (وإلا تُرسم sprites قديمة) ومسح `localStorage`. بعد أي تحميل يكون للمتصفح استدعاءان معلّقان، فيجري tickان في نفس الوقت.
- **الطبقات تُرسم تصاعديًا** (الطبقة 0 أسفل، وفي كل طبقة gc ثم Ib ثم Pa). قاعدة M2 "تنازلي" كانت استنتاجًا من إطار 6 بلا أحداث؛ القائمة الحية تؤكد التصاعدي.
- `r.dg`(1) = **مخفي**؛ `(2,26)` = إخفاء، `(2,27)` = إظهار.
- منطقة التفعيل تستخدم **حجم العرض `Pn/On`** لا حجم الإطار (الإطار 2 عرضه 2500).
- الفأرة تُرى عبر `Eu`: `lj = Gf + ea − (1920/2 − Pn/2)`.
- الأنيميشن الناقصة تشير إلى بديل حسب `La.ZC`، فالتبديل إليها لا يصفّر العدّاد.
- لا امتداد Perspective (type 33) في هذا البناء: لا يُنشأ.
- العدادات type 1 مرسومة بمحاذاة يمنى على `(x − عرض الأرقام، y − الارتفاع)`.
- المزج: float مع اقتطاع هو الأقرب لـChrome.

---

## 7) تحديث 2026-10-19 (الجلسة التالية)

- ✅ `layers.json` مكتمل (الإطار 13 مستنتج)؛ الحزمة أُعيد بناؤها؛ **M3: 15 من 16 إطارًا متطابقة** (`2ac3e5b`). القسم 2 أعلاه صار قديمًا (البنود 1–3 حُلّت).
- ✅ **كل ملفات `engine/data` و`engine/game` تُترجم بـ`powerpc-xsdk-elf-g++ -mcpu=cell -fno-pic -std=gnu++17` بلا أخطاء.**
- **العرض على الجهاز (الخطوة التالية):** نموذج `third_party/xsdk/tests/test_texture.c` (كواد مُنسَّج → eDRAM → `xsdk_gpu_resolve` إلى مخزن عرض). الشيدر الحالي (`tools/gen_texture_shaders.py`، microcode مباشر عبر xost) يُخرج لون النسيج فقط. **ألفا لكل sprite تحتاج ضرب النسيج بلون من الرأس**: وسّع المولّد بجلب لون (FLOAT4) وتمريره كـinterpolator 1، وضربه في الـpixel shader (ALU mul). لا يوجد glslang على الجهاز المضيف، لكن `xsc.native` يعمل لـSPIR-V جاهز إن لزم.
- المزج: blend `SRC_ALPHA / INV_SRC_ALPHA` (الإضافي للحبر 9: `ONE`).

---

## 8) تحديث 2026-10-19 (الدفعة الكاملة): اللعبة قابلة للعب، الحزمة جاهزة

- **الحزمة:** `dist/ucn360/` و`dist/ucn360.zip` (131MB): `default.xex` و`bundle.bin` و`images.pak` و`masks.pak` و`sounds.pak` و`fonts.pak` و`README.txt` (التثبيت + خريطة اليد بالعربية). **لم تُجرَّب على الجهاز بعد.**
- **العرض:** على المعالج (`engine/render/world.cpp`)، لا GPU. يُبنى أولًا قائمة أوامر على الخيط الرئيسي (جلب الصور يفك DXT ويطرد من LRU، فيبقى على خيط واحد). ثم تُرسم 6 شرائط أفقية بالتوازي عبر `render::Parallel`. التنفيذ `engine/render/band_pool.h`: على الجهاز 5 عمال على الخيوط 1–5 والرئيسي على 0. على host يُفعَّل بـ`UCN_THREADS=N`. فحص TSan بـclang نظيف.
- **تسريعات (المخرَج متطابق بايتًا أو ضمن فحص البكسل):**
  - تخطّي ألفا 0.
  - مزج بأعداد صحيحة مع globalAlpha محسوب مرة لكل sprite (تحويل float→int لكل بكسل يوقف نواة PPC).
  - مسار مُكبَّر بلا دوران قابل للفصل: جداول أعمدة وصفوف، bilinear بمرورين على قيم مضروبة مسبقًا (premultiplied)، بلا قسمة لكل بكسل.
  - **الإخفاء:** كل ما تحت sprite معتم يغطي الشاشة كاملة يُتخطّى. حقل `Image::opaque` يُحسب مرة واحدة.
- **الأرقام:**
  - **host بمقياس الجهاز، خيط واحد:** المكتب 8.6ms. مع overlay الإغماء 15ms. حالة الإغماء الكامل كانت 110ms وصارت 12ms. الشاشة (Monitor) بالحجم الكامل 50–60ms.
  - **Xenia:** المكتب 60 tick/ث و48–60 رسمة/ث طوال 8000+ tick (كان 3–19).
- **تحقق المنطق:**
  - مشهد المكتب مع الأزرار (أبواب، فتحات، شاشة، مروحة، كشاف، نقر، 1، X) يطابق الحي 1500 tick. البكسل mean 0.375.
  - المكتب بلا إدخال يطابق الحي 1600 tick. البكسل 0.301 مع overlay الإغماء.
  - مسارات كاملة على host بلا أكواد غير منقولة:
    - شاشة الصوت + Enter → القائمة.
    - "SET ALL 20" + GO → خريطة التحكم → المكتب → الموت (tick ~1980) → Game Over → Enter → إطار 13 → القائمة → GO.
    - ليلة كاملة بلا موت → 6AM (إطار 5) → 13 → القائمة.
- **أدوات تشخيص:**
  - ملف `start_frame` (رقم) بجانب الـxex يبدأ من ذلك الإطار.
  - ملف `dump_frame` يفرّغ RGBA الإطار عند tick 300 إلى `cache:\frame_dump.rgba`.
  - `game_host` مع `UCN_BENCH=1` و`--png` يوقّت الرسم عند tick.
  - `frame_render` مع `UCN_BENCH` و`UCN_THREADS`.
  - **تنبيه:** `--input` في `game_host` بتيكات الإطار المحلية، ويتكرر في كل إطار.
- **التالي إن لزم بعد تجربة الجهاز:**
  1. إن كان الرسم بطيئًا على الجهاز، فالخطوة الكبيرة هي مسار GPU (القسم 7).
  2. الحفظ يكتب `game:\ucn_save.dat`، وهو للقراءة فقط في Xenia فقط.
