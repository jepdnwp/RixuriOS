# RixuriOS xHCI Debug Kuralları (bağlayıcı)

Amaç: Gerçek AMD donanımında USB enumeration'ı çalışır hale getirmek.
Teşhis tek başına hedef değildir; kanıtsız düzeltme yasaktır.

## 1. Korunanlar — DOKUNMA

Çalıştığı kanıtlanmış düzeltmeler, regression testiyle korunur:

- PORTSC/PED RW1C + port reset yolu
- SETUP `uint64_t` packing
- Status Stage DIR (IN→DIR=0, Linux formülü)
- `wait_transfer` TD-aralığı eşleştirmesi
- Address Device + addr postcondition
- Reset Endpoint + Set Dequeue recovery zinciri
- SET_CONFIGURATION eklemesi
- EP0 retry cycle restore, CERR, Evaluate MPS, fresh-slot retry

## 2. Kanıt şartı — ispatlanmadan YASAK

Delay (herhangi ms), retry sayısı/süresi, TSP, Set Dequeue, cache flush,
AMD quirk, port/controller reset, MPS değişikliği, magic constant,
device-specific hack. Değişiklik ancak şu beşli tamamsa yapılır:
**BUG / EVIDENCE / SPEC / LINUX / FIX**.

## 3. Kanıt hiyerarşisi (güçlüden zayıfa)

1. Ham log satırı (register/context/TRB hex + timestamp)
2. Kaynak kod incelemesi (dosya:satır)
3. xHCI spec maddesi + Linux kaynak alıntısı
4. Disassembly/object kanıtı
5. QEMU davranışı (yalnızca negatif kanıt: "QEMU'da da bozuk" anlamlıdır,
   "QEMU'da çalışıyor" AMD için kanıt değildir)

## 4. Değişiklik disiplini

- Minimal, tek amaçlı hunk; refactor yok.
- Davranış-değiştiren her değişiklik öncesi gerekçe (Kural 2 formatı).
- Salt-okur diagnostik serbest kategoridir ama amaçsız log trawling yok.
- Her değişiklik sonrası: `make clean/all/test` + QEMU probe + yeni fingerprint.

## 5. Test protokolü

- Flash öncesi `BUILD:` satırı fingerprint ile doğrulanır; uymuyorsa test geçersiz.
- Aynı cihaz + aynı fiziksel port + aynı controller.
- Hüküm skalası: A) cc=1 → kapandı · B) yine cc=4 → dal planına dön ·
  C) timeout → takılma noktası · D) başka kod → kodu+satırı.

## 6. Dürüstlük kuralları

- Kök neden ispatlanmadıysa `ROOT CAUSE NOT PROVEN` yazılır, iş bırakılmaz —
  bir sonraki ayırt edici test belirtilir.
- "Muhtemelen" ile functional fix uygulanmaz; tahmin "fixed" diye sunulmaz.
- QEMU sonucu AMD sonucu gibi sunulmaz.
- Kanıt/hipotez ayrımı her raporda açık yazılır.
- Donmuş doğrular yeniden tartışmaya açılmaz (örn. STATUS DIR, EP ID=1,
  halted→reset gerekliliği).

## 7. Rapor formatı

`ROOT CAUSE / WHY QEMU PASSES / WHY AMD FAILS / FIRST DIVERGENCE / FIX /
FILES CHANGED / QEMU / AMD / REGRESSION / REMAINING` — AMD satırı donanım
koşusu olmadan `PENDING` kalır.
