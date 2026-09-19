#include "core/i18n.hpp"
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>

namespace eng::i18n {

static Lang g_lang = Lang::En;

// English source -> Turkish. Keep format specifiers identical to the source string.
static const std::unordered_map<std::string, const char*>& turkish() {
    static const std::unordered_map<std::string, const char*> t = {
    {"  Directional Light", "  Yönlü Işık"},
    {"  Panel", "  Panel"},
    {"  Particle Emitter", "  Parçacık Yayıcı"},
    {"  Point Light", "  Nokta Işık"},
    {"  Progress Bar", "  İlerleme Çubuğu"},
    {"  Spot Light", "  Spot Işık"},
    {"  Text", "  Metin"},
    {"%.0f FPS   %d ents   sel: %s   %s", "%.0f FPS   %d nesne   seçili: %s   %s"},
    {"%d ents  %d draws  %.2f ms", "%d nesne  %d çizim  %.2f ms"},
    {"%d selected  (Ctrl+click)", "%d seçili  (Ctrl+tıkla)"},
    {"(scene JSON is the asset)", "(sahne JSON'u varlığın kendisidir)"},
    {"+ Body", "+ Fizik Gövdesi"},
    {"+ Mesh", "+ Mesh"},
    {"Ambient Occlusion (SSAO)", "Ortam Tıkanması (SSAO)"},
    {"Animation", "Animasyon"},
    {"Animations", "Animasyonlar"},
    {"Asset", "Varlık"},
    {"Assets", "Varlıklar"},
    {"Base Color", "Ana Renk"},
    {"Clear", "Temizle"},
    {"Cmd", "Komut"},
    {"Color", "Renk"},
    {"Compile", "Derle"},
    {"Console", "Konsol"},
    {"Debug", "Hata Ayıklama"},
    {"Delete", "Sil"},
    {"Delete Selected", "Seçileni Sil"},
    {"Designer", "Tasarım"},
    {"Details", "Ayrıntılar"},
    {"Direction", "Yön"},
    {"Directional Light", "Yönlü Işık"},
    {"Edit", "Düzenle"},
    {"Emissive", "Işıma"},
    {"FX", "Efektler"},
    {"File", "Dosya"},
    {"Frame stats", "Kare istatistikleri"},
    {"Graph", "Grafik"},
    {"Grid", "Izgara"},
    {"Help", "Yardım"},
    {"Hierarchy", "Hiyerarşi"},
    {"Inner", "İç"},
    {"Intensity", "Yoğunluk"},
    {"LIGHT", "IŞIK"},
    {"Location", "Konum"},
    {"Mass", "Kütle"},
    {"Metallic", "Metalik"},
    {"Move", "Taşı"},
    {"New Scene", "Yeni Sahne"},
    {"No Animation Selected", "Animasyon Seçilmedi"},
    {"Open", "Aç"},
    {"Outer", "Dış"},
    {"Output Log", "Çıktı Günlüğü"},
    {"PREFABS", "HAZIR NESNELER"},
    {"PRIMITIVE", "TEMEL ŞEKİLLER"},
    {"Palette", "Palet"},
    {"Playing", "Oynatılıyor"},
    {"Point / Spot Light", "Nokta / Spot Işık"},
    {"Primitive", "Temel Şekil"},
    {"Radius", "Yarıçap"},
    {"Range", "Menzil"},
    {"Redo", "Yinele"},
    {"Refresh", "Yenile"},
    {"Reload", "Yeniden Yükle"},
    {"Reset Layout", "Düzeni Sıfırla"},
    {"Restitution", "Sekme"},
    {"Rigid Body", "Katı Cisim"},
    {"Rotate", "Döndür"},
    {"Rotation", "Dönüş"},
    {"Roughness", "Pürüzlülük"},
    {"SCENES", "SAHNELER"},
    {"Save", "Kaydet"},
    {"Scale", "Ölçek"},
    {"Select an object", "Bir nesne seçin"},
    {"Skinned entities", "İskeletli nesneler"},
    {"Snap", "Yakala"},
    {"Speed", "Hız"},
    {"Step", "Adım"},
    {"Strength", "Güç"},
    {"Timeline", "Zaman Çizelgesi"},
    {"Tools", "Araçlar"},
    {"Transform", "Dönüşüm"},
    {"Type", "Tür"},
    {"UI", "Arayüz"},
    {"Undo", "Geri Al"},
    {"View", "Görünüm"},
    {"Window", "Pencere"},
    {"ali-engine editor", "ali-engine editörü"},
    {"clip: %s", "klip: %s"},
    {"color", "renk"},
    {"play", "oynat"},
    {"speed", "hız"},
    {"time", "süre"},
    {"Play", "Oynat"},
    {"Stop", "Durdur"},
    {"running", "çalışıyor"},
    {"paused", "duraklatıldı"},
    {"unsaved", "kaydedilmedi"},
    {"EDIT", "DÜZENLE"},
    {"PLAYING  --  WASD / Space", "OYNATILIYOR  --  WASD / Boşluk"},
    {"Language", "Dil"},
    {"sel", "seçili"},
    {"Search Palette", "Palet ara"},
    {"Search", "Ara"},
    {"Filter properties", "Özellik filtrele"},
    {"Enter JSON Command", "JSON komutu girin"},
    {"JSON command", "JSON komutu"},
    {"scene path", "sahne yolu"},
    {"(no target)", "(hedef yok)"},
    {"  Cube", "  Küp"},
    {"  Sphere", "  Küre"},
    {"  Plane", "  Düzlem"},
    {"  Skinned bar", "  İskeletli çubuk"},
    {"right-click: add node   drag pins: wire   Del: remove   Ctrl+D: duplicate   ·   graph = %s's Behavior", "sağ tık: düğüm ekle   pinleri sürükle: bağla   Del: sil   Ctrl+D: çoğalt   ·   grafik = %s davranışı"},
    {"On Start", "Başlangıçta"},
    {"On Tick", "Her Karede"},
    {"On Collision", "Çarpışınca"},
    {"On Enter", "Girince"},
    {"On Exit", "Çıkınca"},
    {"On Event", "Olay Gelince"},
    {"On Input", "Girdi Olunca"},
    {"On Custom Trigger", "Özel Tetikleyici"},
    {"Impulse", "İtki"},
    {"Set Velocity", "Hız Ata"},
    {"Spin", "Döndür"},
    {"Move Toward", "Hedefe Git"},
    {"Set Color", "Renk Ata"},
    {"Set Material", "Malzeme Ata"},
    {"Spawn", "Oluştur"},
    {"Destroy", "Yok Et"},
    {"Emit Event", "Olay Yayınla"},
    {"Set State", "Durum Ata"},
    {"Add State", "Duruma Ekle"},
    {"Timer", "Zamanlayıcı"},
    {"Set UI", "Arayüzü Ata"},
    {"Play Sound", "Ses Çal"},
    {"Anim Param", "Animasyon Parametresi"},
    {"Log", "Günlük"},
    {"Raw Action (JSON)", "Ham Eylem (JSON)"},
    {"Events", "Olaylar"},
    {"Physics", "Fizik"},
    {"Transform", "Dönüşüm"},
    {"Material", "Malzeme"},
    {"Scene", "Sahne"},
    {"Logic", "Mantık"},
    {"Audio", "Ses"},
    {"Animation", "Animasyon"},
    {"Advanced", "Gelişmiş"},
    {"impulse", "itki"},
    {"velocity", "hız (vektör)"},
    {"axis", "eksen"},
    {"speed_deg", "hız (°/sn)"},
    {"speed", "hız"},
    {"target", "hedef"},
    {"primitive", "şekil"},
    {"position", "konum"},
    {"event", "olay"},
    {"key", "anahtar"},
    {"value", "değer"},
    {"after", "sonra (sn)"},
    {"text", "metin"},
    {"message", "mesaj"},
    {"with", "ile"},
    {"name", "ad"},
    {"file", "dosya"},
    {"volume", "ses"},
    {"loop", "döngü"},
    {"spatial", "uzamsal"},
    {"base_color", "renk"},
    {"metallic", "metalik"},
    {"roughness", "pürüzlülük"},
    {"param", "parametre"},
    {"keep_y", "Y'yi koru"},
    {"input", "girdi"},
    {"mode", "mod"},
    {"on", "tetikleyici"},
    {"operator", "işlem"},
    {"Condition", "Koşul"},
    {"in", "giriş"},
    {"out", "çıkış"},
    {"Search node", "Düğüm ara"},
    {"any action, as JSON", "herhangi bir eylem, JSON olarak"},
    {"invalid JSON (not applied)", "geçersiz JSON (uygulanmadı)"},
    {"Compiled: %d rule(s)", "Derlendi: %d kural"},
    {"  (%d unconnected node(s) skipped)", "  (%d bağlantısız düğüm atlandı)"},
    {"Compile failed: ", "Derleme başarısız: "},
    {"modified - not compiled", "değişti - derlenmedi"},
    {"not connected - will not run", "bağlı değil - çalışmaz"},
    };
    return t;
}

void init() {
    const char* forced = std::getenv("ALI_LANG");
    if (forced && *forced) { g_lang = (std::strncmp(forced, "tr", 2) == 0) ? Lang::Tr : Lang::En; return; }
    for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* v = std::getenv(var);
        if (v && *v) { g_lang = (std::strncmp(v, "tr", 2) == 0) ? Lang::Tr : Lang::En; return; }
    }
}

Lang language() { return g_lang; }
void set_language(Lang lang) { g_lang = lang; }

const char* T(const char* en) {
    if (g_lang == Lang::En || !en) return en;
    auto it = turkish().find(en);
    return it == turkish().end() ? en : it->second;
}

const char* L(const char* en) {
    if (!en || !*en || (en[0] == '#' && en[1] == '#')) return en;
    // std::map nodes are stable, so the returned c_str() stays valid for the whole run.
    static std::map<std::pair<int, std::string>, std::string> cache;
    auto key = std::make_pair(static_cast<int>(g_lang), std::string(en));
    auto it = cache.find(key);
    if (it == cache.end()) it = cache.emplace(key, std::string(T(en)) + "###" + en).first;
    return it->second.c_str();
}

} // namespace eng::i18n
