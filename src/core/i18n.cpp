#include "core/i18n.h"

#include <atomic>
#include <cmath>
#include <map>
#include <mutex>

#include "core/util.h"

namespace kayra {

namespace {

struct Text {
    const char* tr;
    const char* en;
};

// clang-format off
const std::map<std::string, Text>& table() {
    static const std::map<std::string, Text> t = {
        // --- time
        {"time_now", {"Saat {0}, {sir}.", "It's {0}, {sir}."}},
        {"date_today", {"Bugün {0}.", "Today is {0}."}},
        {"time_and_date", {"Saat {0}. Bugün {1}.", "It's {0}. Today is {1}."}},
        // --- open / search
        {"open_what", {"Neyi açmamı istersiniz?", "What should I open?"}},
        {"app_not_found", {"“{0}” adında bir uygulama ya da site bulamadım, {sir}.", "I couldn't find an app or site called “{0}”, {sir}."}},
        {"open_failed", {"{0} açılamadı.", "I couldn't open {0}."}},
        {"opening_app", {"{0} açılıyor, {sir}.", "Opening {0}, {sir}."}},
        {"opening_folder", {"{0} klasörünü açıyorum.", "Opening your {0} folder."}},
        {"opening_site", {"{0} açılıyor.", "Opening {0}."}},
        {"opening_link", {"Bağlantıyı açıyorum.", "Opening the link."}},
        {"search_what", {"Neyi aramamı istersiniz?", "What should I search for?"}},
        {"searching", {"{0} “{1}” için arama yapıyorum.", "Searching {0} for “{1}”."}},
        // --- weather
        {"weather_failed", {"Hava durumu servisine ulaşamadım, {sir}.", "I couldn't reach the weather service, {sir}."}},
        {"weather_now", {"{0} hava şu an {1} derece ve {2}.", "In {0} it's {1} degrees and {2}."}},
        {"weather_range", {"Bugün en yüksek {0}, en düşük {1} derece bekleniyor.", "Today's high is {0} and the low {1} degrees."}},
        {"weather_rain", {"Yağış ihtimali yüzde {0}.", "There's a {0} percent chance of rain."}},
        // --- knowledge
        {"wiki_not_found", {"“{0}” hakkında bir bilgi bulamadım.", "I couldn't find anything about “{0}”."}},
        // --- system
        {"sys_cpu_ram", {"İşlemci yüzde {0} yükte, bellek yüzde {1} dolu.", "CPU load is {0} percent and memory is {1} percent full."}},
        {"sys_battery", {"Pil yüzde {0}.", "Battery at {0} percent."}},
        {"sys_battery_charging", {"Pil yüzde {0} ve şarj oluyor.", "Battery at {0} percent and charging."}},
        {"sys_disk", {"C sürücüsünde {0} gigabayt boş alan var.", "There are {0} gigabytes free on drive C."}},
        {"volume_failed", {"Ses denetimine erişemedim.", "I couldn't reach the volume control."}},
        {"volume_muted", {"Ses kapatıldı.", "Muted."}},
        {"volume_now", {"Ses seviyesi yüzde {0}.", "Volume is at {0} percent."}},
        {"media_toggled", {"Oynatma durumunu değiştirdim.", "Toggled playback."}},
        {"media_next", {"Sonraki parça.", "Next track."}},
        {"media_previous", {"Önceki parça.", "Previous track."}},
        {"media_stopped", {"Oynatma durduruldu.", "Playback stopped."}},
        {"media_paused", {"Duraklatıldı.", "Paused."}},
        {"media_playing", {"Oynatılıyor.", "Playing."}},
        {"screenshot_failed", {"Ekran görüntüsü alınamadı.", "I couldn't take a screenshot."}},
        {"screenshot_saved", {"Ekran görüntüsünü aldım; Resimler klasöründe, Kayra altında.", "Screenshot saved to Pictures, in the Kayra folder."}},
        {"locking", {"Bilgisayar kilitleniyor, {sir}.", "Locking the computer, {sir}."}},
        {"power_cancelled", {"Kapatma iptal edildi.", "Shutdown cancelled."}},
        {"power_unknown", {"Bunu yapamam.", "I can't do that."}},
        {"power_shutdown", {"Bilgisayarı kapatmamı", "shut down the computer"}},
        {"power_restart", {"Bilgisayarı yeniden başlatmamı", "restart the computer"}},
        {"power_sleep", {"Bilgisayarı uyku moduna almamı", "put the computer to sleep"}},
        {"confirm_power", {"{0} istediğinize emin misiniz, {sir}? Onaylamak için “evet” deyin.", "Do you want me to {0}, {sir}? Say “yes” to confirm."}},
        {"power_go_shutdown", {"Bilgisayar bir dakika içinde kapanacak. Vazgeçerseniz “kapatmayı iptal et” deyin.", "The computer will shut down in one minute. Say “cancel shutdown” to stop it."}},
        {"power_go_restart", {"Bilgisayar bir dakika içinde yeniden başlayacak. Vazgeçerseniz “kapatmayı iptal et” deyin.", "The computer will restart in one minute. Say “cancel shutdown” to stop it."}},
        {"power_go_sleep", {"İyi geceler, {sir}.", "Good night, {sir}."}},
        {"confirm_cancelled", {"Peki, vazgeçtim.", "Alright, cancelled."}},
        {"clipboard_empty", {"Panoda metin yok.", "There's no text on the clipboard."}},
        {"clipboard_read", {"Panodaki metni okudum.", "I read the clipboard."}},
        {"clipboard_says", {"Panoda şu yazıyor: {0}", "The clipboard says: {0}"}},
        {"calc_failed", {"Bunu hesaplayamadım.", "I couldn't calculate that."}},
        {"calc_result", {"Sonuç {0}.", "That's {0}."}},
        {"shell_disabled", {"Komut çalıştırma ayarlarda kapalı.", "Running commands is turned off in the settings."}},
        {"command_failed", {"Komut çalıştırılamadı.", "The command could not run."}},
        {"command_done", {"Komut çalıştı.", "The command ran."}},
        {"tool_error", {"Bir sorun çıktı, {sir}.", "Something went wrong, {sir}."}},
        // --- files
        {"files_disabled", {"Dosya erişimi ayarlarda kapalı.", "File access is turned off in the settings."}},
        {"folder_not_found", {"{0} adlı bir klasör bulamadım.", "I couldn't find a folder called {0}."}},
        {"folder_empty", {"{0} boş, {sir}.", "{0} is empty, {sir}."}},
        {"folder_list", {"{0} klasöründe {1} öğe var: {2}.", "{0} has {1} items: {2}."}},
        {"folder_list_more", {"{0} klasöründe {1} öğe var. En yenileri: {2}.", "{0} has {1} items. The newest: {2}."}},
        {"and_more", {"ve {0} tane daha", "and {0} more"}},
        {"files_found_one", {"Buldum: {0}, {1} klasöründe.", "Found it: {0}, in {1}."}},
        {"files_found", {"{0} sonuç buldum. İlki {1}, {2} klasöründe.", "I found {0} matches. The first is {1}, in {2}."}},
        {"files_none", {"“{0}” adında bir dosya bulamadım.", "I couldn't find a file named “{0}”."}},
        {"file_not_found", {"{0} dosyasını bulamadım.", "I couldn't find {0}."}},
        {"file_read", {"{0} dosyasında şunlar yazıyor: {1}", "{0} says: {1}"}},
        {"file_empty", {"{0} dosyası boş.", "{0} is empty."}},
        {"file_binary", {"{0} bir metin dosyası değil, okuyamıyorum. İsterseniz açabilirim.", "{0} isn't a text file, so I can't read it. I can open it for you."}},
        {"file_written", {"{0} dosyasını kaydettim.", "I saved {0}."}},
        {"file_appended", {"{0} dosyasına ekledim.", "I added it to {0}."}},
        {"file_write_failed", {"{0} dosyasına yazamadım.", "I couldn't write to {0}."}},
        {"confirm_overwrite", {"{0} zaten var. Üzerine yazmamı onaylıyor musunuz, {sir}?", "{0} already exists. Should I overwrite it, {sir}?"}},
        {"confirm_delete", {"{0} çöp kutusuna taşınsın mı, {sir}? Onaylamak için “evet” deyin.", "Move {0} to the Recycle Bin, {sir}? Say “yes” to confirm."}},
        {"deleted", {"{0} çöp kutusuna taşındı.", "{0} is in the Recycle Bin now."}},
        {"delete_failed", {"{0} silinemedi.", "I couldn't delete {0}."}},
        {"moved", {"{0}, {1} konumuna taşındı.", "Moved {0} to {1}."}},
        {"copied", {"{0}, {1} konumuna kopyalandı.", "Copied {0} to {1}."}},
        {"move_failed", {"{0} taşınamadı.", "I couldn't move {0}."}},
        {"copy_failed", {"{0} kopyalanamadı.", "I couldn't copy {0}."}},
        {"target_exists", {"{0} zaten var; üzerine yazmıyorum.", "{0} already exists; I won't overwrite it."}},
        {"folder_created", {"{0} klasörünü oluşturdum.", "I created the folder {0}."}},
        {"folder_create_failed", {"{0} klasörünü oluşturamadım.", "I couldn't create the folder {0}."}},
        {"opening_file", {"{0} açılıyor.", "Opening {0}."}},
        // --- web
        {"web_disabled", {"İnternet erişimi ayarlarda kapalı.", "Web access is turned off in the settings."}},
        {"web_failed", {"İnternete ulaşamadım, {sir}.", "I couldn't reach the internet, {sir}."}},
        {"web_none", {"“{0}” için bir sonuç bulamadım.", "I found nothing for “{0}”."}},
        {"web_top", {"İnternette ilk sonuç {0}: {1}", "The top result is {0}: {1}"}},
        {"news_none", {"Şu an haber alamadım.", "I couldn't get the news right now."}},
        {"news_top", {"Öne çıkan haberler: {0}.", "Top headlines: {0}."}},
        {"news_about", {"{0} hakkında son haberler: {1}.", "Latest on {0}: {1}."}},
        {"page_read", {"{0} sayfasını okudum.", "I read {0}."}},
        {"page_failed", {"Sayfayı okuyamadım.", "I couldn't read the page."}},
        // --- timers and memory
        {"timer_how_long", {"Ne kadar süreliğine kurayım?", "For how long?"}},
        {"timer_too_long", {"Bir haftadan uzun zamanlayıcı kuramıyorum.", "I can't set timers longer than a week."}},
        {"timer_set", {"{0} için zamanlayıcı kuruldu, {sir}.", "Timer set for {0}, {sir}."}},
        {"reminder_set", {"Tamam {sir}, {0} sonra {1} hatırlatacağım.", "Okay {sir}, I'll remind you in {0}: {1}."}},
        {"timers_cancelled", {"{0} zamanlayıcı iptal edildi.", "Cancelled {0} timer(s)."}},
        {"no_timers", {"Çalışan bir zamanlayıcı yok.", "There are no running timers."}},
        {"timer_done", {"{Sir}, zamanlayıcı doldu.", "{Sir}, your timer is done."}},
        {"reminder_done", {"{Sir}, {0} hatırlatmamı istemiştiniz.", "{Sir}, you asked me to remind you: {0}."}},
        {"remember_what", {"Neyi hatırlamamı istersiniz?", "What should I remember?"}},
        {"remembered", {"Not ettim, {sir}. Bunu unutmayacağım.", "Noted, {sir}. I'll remember that."}},
        {"forgot", {"{0} bilgiyi unuttum.", "Forgot {0} item(s)."}},
        {"forgot_none", {"Bununla ilgili hatırladığım bir şey yok.", "I don't remember anything about that."}},
        {"memory_empty", {"Henüz hatırladığım bir bilgi yok.", "I don't have anything saved yet."}},
        {"memory_list", {"Hatırladıklarım:", "Here's what I remember:"}},
        // --- conversation
        {"greet_morning", {"Günaydın, {sir}.", "Good morning, {sir}."}},
        {"greet_day", {"İyi günler, {sir}.", "Good afternoon, {sir}."}},
        {"greet_evening", {"İyi akşamlar, {sir}.", "Good evening, {sir}."}},
        {"greet_night", {"İyi geceler, {sir}. Geç oldu.", "Good evening, {sir}. It's getting late."}},
        {"hello", {"Merhaba, {sir}. Size nasıl yardımcı olabilirim?", "Hello, {sir}. How can I help?"}},
        {"how_are_you", {"Tüm sistemler normal çalışıyor, {sir}. Siz nasılsınız?", "All systems are running smoothly, {sir}. How are you?"}},
        {"glad", {"Bunu duyduğuma sevindim, {sir}.", "Glad to hear it, {sir}."}},
        {"thanks", {"Rica ederim, {sir}. Her zaman.", "You're welcome, {sir}. Always."}},
        {"who_are_you", {"Ben Kayra, kişisel yapay zekâ asistanınız. Bilgisayarınızı yönetir, sorularınızı yanıtlar ve hatırlatmalarınızı takip ederim.",
                         "I'm Kayra, your personal AI assistant. I run your computer, answer your questions and keep track of your reminders."}},
        {"who_made_you", {"Beni Hydra-Ai-tr geliştirdi; açık kaynak bir projeyim.", "I was built by Hydra-Ai-tr. I'm an open-source project."}},
        {"help", {"Saati ve tarihi söyleyebilir, uygulama, klasör ve site açabilir, dosyalarınızı bulup okuyabilir, internette arama yapıp sonuçları ve haberleri okuyabilir, hava durumunu ve sistem durumunu bildirebilir, sesi ve müziği yönetebilir, zamanlayıcı ve hatırlatma kurabilir, hesap yapabilir ve söylediklerinizi hatırlayabilirim. Bir yapay zekâ beyni bağlarsanız her konuda sohbet de edebiliriz.",
                  "I can tell the time and date, open apps, folders and sites, find and read your files, search the web and read results and the news, report the weather and system status, control volume and music, set timers and reminders, do math and remember things for you. Connect an AI brain and we can talk about anything."}},
        {"not_understood", {"Bunu henüz anlayamadım, {sir}. Serbest sohbet için Ayarlar'dan bir yapay zekâ beyni bağlayabilirsiniz.",
                            "I didn't catch that, {sir}. For open conversation, connect an AI brain in the settings."}},
        {"brain_error", {"Yapay zekâ beynine ulaşamadım, {sir}. {0}", "I couldn't reach the AI brain, {sir}. {0}"}},
        {"brain_quota", {"Ücretsiz kullanım sınırı doldu; bir dakika sonra tekrar deneyin.", "The free usage limit is reached; try again in a minute."}},
        {"brain_busy", {"Sunucu şu an çok yoğun; birazdan tekrar deneyin ya da Ayarlar'dan başka bir model seçin.",
                        "The server is very busy; try again shortly or pick another model in the settings."}},
        {"brain_key", {"API anahtarı geçersiz görünüyor.", "The API key looks invalid."}},
        {"brain_model", {"Bu model artık yok; Ayarlar'dan güncel bir model seçin.", "That model no longer exists; pick a current one in the settings."}},
        {"brain_offline_try", {"Yerleşik komutlarla devam ediyorum.", "Falling back to built-in commands."}},
        {"wake_ack", {"Buyurun, {sir}?", "Yes, {sir}?"}},
        {"ok", {"Peki, {sir}.", "Very well, {sir}."}},
        {"title_set", {"Peki, bundan sonra size “{0}” diye hitap edeceğim.", "Alright, I'll call you “{0}” from now on."}},
        {"goodbye", {"Görüşmek üzere, {sir}.", "Goodbye, {sir}."}},
        {"online", {"Kayra çevrimiçi. Tüm sistemler hazır.", "Kayra online. All systems ready."}},
        {"joke", {"Bir yazılımcı markete gitmiş. Eşi demiş ki: bir ekmek al, yumurta varsa on tane al. On ekmekle dönmüş.",
                  "A programmer's partner says: buy a loaf of bread, and if they have eggs, get a dozen. He comes home with twelve loaves."}},
        {"user_name_set", {"Memnun oldum, {0}. Artık sizi tanıyorum.", "Nice to meet you, {0}. I'll remember that."}},
        {"your_name_is", {"Adınız {0}.", "Your name is {0}."}},
        {"dont_know_name", {"Henüz adınızı bilmiyorum. “Benim adım ...” diyerek söyleyebilirsiniz.", "I don't know your name yet. Tell me with “my name is ...”."}},
        {"mic_needs_model", {"Ses tanıma modeli indiriliyor, birazdan sizi duyabileceğim.", "Downloading the speech model; I'll be able to hear you shortly."}},
        // --- HUD
        {"ui_idle", {"BEKLEMEDE", "STANDBY"}},
        {"ui_listening", {"DİNLİYORUM", "LISTENING"}},
        {"ui_hearing", {"DUYUYORUM", "HEARING"}},
        {"ui_transcribing", {"ÇÖZÜMLÜYORUM", "TRANSCRIBING"}},
        {"ui_thinking", {"DÜŞÜNÜYORUM", "THINKING"}},
        {"ui_speaking", {"KONUŞUYOR", "SPEAKING"}},
        {"ui_waiting_wake", {"“Kayra” diye seslenin", "Say “Kayra”"}},
        {"ui_mic_off_hint", {"Mikrofon kapalı · Ctrl+Shift+K ile konuşun", "Microphone off · press Ctrl+Shift+K to talk"}},
        {"ui_push_hint", {"Konuşmak için Ctrl+Shift+K", "Press Ctrl+Shift+K to talk"}},
        {"ui_input_hint", {"Kayra'ya yazın ve Enter'a basın…", "Type to Kayra and press Enter…"}},
        {"ui_system", {"SİSTEM", "SYSTEM"}},
        {"ui_cpu", {"İŞLEMCİ", "CPU"}},
        {"ui_ram", {"BELLEK", "MEMORY"}},
        {"ui_battery", {"PİL", "BATTERY"}},
        {"ui_disk", {"DİSK C:", "DISK C:"}},
        {"ui_uptime", {"AÇIK KALMA", "UPTIME"}},
        {"ui_processes", {"İŞLEM", "PROCESSES"}},
        {"ui_conversation", {"KONUŞMA", "CONVERSATION"}},
        {"ui_you", {"SİZ", "YOU"}},
        {"ui_weather", {"HAVA", "WEATHER"}},
        {"ui_timers", {"ZAMANLAYICILAR", "TIMERS"}},
        {"ui_no_timers", {"Etkin zamanlayıcı yok", "No active timers"}},
        {"ui_brain", {"BEYİN", "BRAIN"}},
        {"ui_builtin", {"Yerleşik komutlar", "Built-in commands"}},
        {"ui_voice", {"SES", "VOICE"}},
        {"ui_mic", {"MİKROFON", "MICROPHONE"}},
        {"ui_on", {"AÇIK", "ON"}},
        {"ui_off", {"KAPALI", "OFF"}},
        {"ui_settings", {"AYARLAR", "SETTINGS"}},
        {"ui_downloading", {"Ses modeli indiriliyor %{0}", "Downloading speech model {0}%"}},
        {"ui_loading_model", {"Ses modeli yükleniyor…", "Loading speech model…"}},
        {"ui_stt_missing", {"Bu sürüm ses tanıma olmadan derlendi", "This build has no speech recognition"}},
        {"ui_mic_error", {"Mikrofon açılamadı", "Could not open the microphone"}},
        {"ui_heard", {"DUYULAN", "HEARD"}},
        {"ui_clear", {"Temizle", "Clear"}},
        {"ui_core", {"ÇEKİRDEK", "CORE"}},
        {"ui_online", {"ÇEVRİMİÇİ", "ONLINE"}},
        {"ui_preparing", {"HAZIRLANIYOR", "PREPARING"}},
        {"ui_error", {"HATA", "ERROR"}},
        {"ui_mode_wake", {"UYANDIRMA: “KAYRA”", "WAKE WORD: “KAYRA”"}},
        {"ui_mode_always", {"SÜREKLİ DİNLEME", "ALWAYS LISTENING"}},
        {"ui_mode_push", {"BAS-KONUŞ", "PUSH TO TALK"}},
        {"ui_mode_off", {"MİKROFON KAPALI", "MICROPHONE OFF"}},
        {"ui_talk", {"KONUŞ", "TALK"}},
        {"nav_home", {"Ana Ekran", "Home"}},
        {"nav_system", {"Sistem Analizi", "System Analysis"}},
        {"nav_brain", {"Beyin / Yapay Zekâ", "Brain / AI"}},
        {"nav_chat", {"Konuşma", "Conversation"}},
        {"nav_tools", {"Araçlar", "Tools"}},
        {"nav_modules", {"Eklentiler", "Plugins"}},
        {"nav_timers", {"Zamanlayıcılar", "Timers"}},
        {"nav_settings", {"Ayarlar", "Settings"}},
        {"tab_dashboard", {"Dashboard", "Dashboard"}},
        {"tab_brain", {"Beyin", "Brain"}},
        {"tab_system", {"Sistem", "System"}},
        {"tab_tools", {"Araçlar", "Tools"}},
        {"tab_settings", {"Ayarlar", "Settings"}},
        {"hi_morning", {"Günaydın", "Good morning"}},
        {"hi_day", {"İyi günler", "Good afternoon"}},
        {"hi_evening", {"İyi akşamlar", "Good evening"}},
        {"hi_night", {"İyi geceler", "Good night"}},
        {"head_ready", {"hazır.", "is ready."}},
        {"head_listening", {"dinliyor.", "is listening."}},
        {"head_hearing", {"sizi duyuyor.", "hears you."}},
        {"head_transcribing", {"anlıyor…", "is understanding…"}},
        {"head_thinking", {"düşünüyor…", "is thinking…"}},
        {"head_speaking", {"konuşuyor.", "is speaking."}},
        {"head_preparing", {"hazırlanıyor…", "is getting ready…"}},
        {"head_error", {"bir sorunla karşılaştı.", "hit a problem."}},
        {"sub_ok", {"Sistemler aktif, tüm modüller çalışıyor.", "Systems active, all modules running."}},
        {"chip_online", {"Çevrimiçi", "Online"}},
        {"chip_offline", {"Çevrimdışı", "Offline"}},
        {"chip_brain_on", {"Beyin Aktif", "Brain Active"}},
        {"chip_brain_off", {"Yerleşik Komutlar", "Built-in Commands"}},
        {"chip_stable", {"Sistem Stabil", "System Stable"}},
        {"chip_busy", {"Sistem Yoğun", "System Busy"}},
        {"footer_sub", {"Kişisel Yapay Zekâ", "Personal AI"}},
        {"core_sub", {"KİŞİSEL YAPAY ZEKÂ", "PERSONAL AI"}},
        {"btn_mic", {"Mikrofon", "Microphone"}},
        {"btn_voice", {"Ses", "Voice"}},
        {"btn_talk", {"Konuşma", "Talk"}},
        {"btn_system", {"Sistem", "System"}},
        {"btn_system_sub", {"Yönet", "Manage"}},
        {"btn_modules", {"Eklentiler", "Plugins"}},
        {"on", {"Açık", "On"}},
        {"off", {"Kapalı", "Off"}},
        {"n_active", {"{0} Aktif", "{0} Active"}},
        {"card_system", {"Sistem Durumu", "System Status"}},
        {"card_perf", {"Performans", "Performance"}},
        {"perf_cpu", {"İşlemci · Son 60 Saniye", "CPU · Last 60 Seconds"}},
        {"perf_ram", {"Bellek · Son 60 Saniye", "Memory · Last 60 Seconds"}},
        {"card_weather", {"Hava Durumu", "Weather"}},
        {"weather_loading", {"Hava durumu alınıyor…", "Getting the weather…"}},
        {"weather_none", {"Hava durumuna ulaşılamadı", "Weather unavailable"}},
        {"card_timers", {"Zamanlayıcılar", "Timers"}},
        {"card_chat", {"Konuşma", "Conversation"}},
        {"lbl_cpu", {"İşlemci", "CPU"}},
        {"lbl_ram", {"Bellek", "Memory"}},
        {"lbl_disk", {"Disk (C:)", "Disk (C:)"}},
        {"lbl_battery", {"Pil", "Battery"}},
        {"lbl_uptime", {"Açık kalma", "Uptime"}},
        {"lbl_humidity", {"Nem", "Humidity"}},
        {"lbl_wind", {"Rüzgâr", "Wind"}},
        {"lbl_feels", {"Hissedilen", "Feels like"}},
        {"no_timers", {"Etkin zamanlayıcı yok.", "No active timers."}},
        {"timer_default", {"Zamanlayıcı", "Timer"}},
        {"timer_prefix", {"10 dakika sonra hatırlat: ", "remind me in 10 minutes to "}},
        {"timer_quick", {"{0} dakikalık zamanlayıcı kur", "set a timer for {0} minutes"}},
        {"quick_timer", {"Hızlı zamanlayıcı", "Quick timer"}},
        {"minutes", {"dakika", "minutes"}},
        {"custom_reminder", {"Özel hatırlatma yaz…", "Write a custom reminder…"}},
        {"menu_clear", {"Konuşmayı temizle", "Clear the conversation"}},
        {"input_hint", {"Kayra'ya yazın...", "Type to Kayra..."}},
        {"last_60", {"Son 60 saniye", "Last 60 seconds"}},
        {"sys_details", {"Ayrıntılar", "Details"}},
        {"sys_computer", {"Bilgisayar", "Computer"}},
        {"sys_cpu_model", {"İşlemci modeli", "Processor"}},
        {"sys_ram_used", {"Kullanılan bellek", "Memory in use"}},
        {"sys_disk_free", {"Boş disk alanı", "Free disk space"}},
        {"sys_processes", {"Çalışan işlem", "Running processes"}},
        {"tools_hint", {"Bir örneğe tıklayın, Kayra hemen yapsın. Sesle de aynısını söyleyebilirsiniz.",
                        "Click an example and Kayra does it. You can say the same out loud."}},
        {"modules_hint", {"Kayra'nın neleri kullanabileceğini seçin. Değişiklikler hemen kaydedilir.",
                          "Choose what Kayra may use. Changes are saved right away."}},
        {"mod_tools", {"Araç kullanımı", "Tool use"}},
        {"mod_files", {"Dosya erişimi", "File access"}},
        {"mod_web", {"İnternet erişimi", "Web access"}},
        {"mod_shell", {"Konsol komutları", "Console commands"}},
        {"attach_cmd", {"\"{0}\" dosyasını oku", "read the file \"{0}\""}},
        {"mod_on", {"● Etkin", "● Enabled"}},
        {"mod_off", {"○ Kapalı", "○ Disabled"}},
        // --- settings window
        {"set_title", {"Kayra Ayarları", "Kayra Settings"}},
        {"set_general", {"Genel", "General"}},
        {"set_brain", {"Beyin (LLM)", "Brain (LLM)"}},
        {"set_voice", {"Ses", "Voice"}},
        {"set_listen", {"Dinleme", "Listening"}},
        {"set_memory", {"Hafıza", "Memory"}},
        {"set_language", {"Dil", "Language"}},
        {"set_name", {"Adınız", "Your name"}},
        {"set_title_addr", {"Hitap (boş = efendim)", "Address (empty = sir)"}},
        {"set_city", {"Şehir (boş = otomatik)", "City (empty = automatic)"}},
        {"set_autostart", {"Windows açılışında başlat", "Start with Windows"}},
        {"set_provider", {"Sağlayıcı", "Provider"}},
        {"set_base_url", {"API adresi", "API address"}},
        {"set_api_key", {"API anahtarı", "API key"}},
        {"set_model", {"Model", "Model"}},
        {"set_temperature", {"Yaratıcılık", "Creativity"}},
        {"set_tools", {"Araçları kullanabilsin (uygulama açma, arama...)", "Let it use tools (open apps, search...)"}},
        {"set_shell", {"Konsol komutu çalıştırabilsin (dikkat)", "Allow console commands (careful)"}},
        {"set_files", {"Dosyalara erişebilsin (listeleme, arama, okuma, yazma; silmeden önce sorar)", "File access (list, find, read, write; asks before deleting)"}},
        {"set_web", {"İnternete erişebilsin (arama sonuçları, haberler, web sayfaları)", "Web access (search results, news, web pages)"}},
        {"set_engine", {"Ses motoru", "Voice engine"}},
        {"set_engine_piper", {"Piper · doğal, bilgisayarda", "Piper · natural, on this PC"}},
        {"set_engine_edge", {"Edge · en doğal, çevrimiçi", "Edge · most natural, online"}},
        {"set_engine_sapi", {"Windows sesleri", "Windows voices"}},
        {"set_engine_note_piper", {"İnternet gerekmez. Seçilen ses ilk kullanımda indirilir (~85 MB).", "No internet needed. The voice downloads on first use (~85 MB)."}},
        {"set_engine_note_edge", {"Microsoft'un yapay sinir ağı sesleri; internet ve edge-tts gerekir (pip install edge-tts). Ulaşılamazsa Windows sesi kullanılır.",
                                  "Microsoft neural voices; needs the internet and edge-tts (pip install edge-tts). Falls back to the Windows voice."}},
        {"set_engine_install", {"edge-tts'i kur", "Install edge-tts"}},
        {"set_engine_ready", {"Hazır", "Ready"}},
        {"set_engine_missing", {"Kurulu değil", "Not installed"}},
        {"set_engine_downloading", {"İndiriliyor %{0}", "Downloading {0}%"}},
        {"set_windows_voice", {"Yedek Windows sesi", "Fallback Windows voice"}},
        {"set_test", {"Bağlantıyı dene", "Test connection"}},
        {"set_brain_note", {"Beyin bağlı değilken Kayra yerleşik komutlarla internetsiz çalışır. Ollama veya LM Studio ile tamamen yerel bir model kullanabilirsiniz.",
                            "Without a brain Kayra runs on built-in commands, offline. Use Ollama or LM Studio for a fully local model."}},
        {"set_speak", {"Yanıtları sesli oku", "Speak replies"}},
        {"set_voice_name", {"Ses", "Voice"}},
        {"set_rate", {"Hız", "Rate"}},
        {"set_volume", {"Ses düzeyi", "Volume"}},
        {"set_try_voice", {"Sesi dene", "Try voice"}},
        {"set_listen_mode", {"Dinleme biçimi", "Listening mode"}},
        {"set_mode_wake", {"Uyandırma kelimesi (“Kayra ...”)", "Wake word (“Kayra ...”)"}},
        {"set_mode_always", {"Her konuşmayı komut say", "Treat all speech as commands"}},
        {"set_mode_push", {"Yalnızca kısayol / düğme", "Hotkey / button only"}},
        {"set_wake_words", {"Uyandırma kelimeleri (virgülle)", "Wake words (comma separated)"}},
        {"set_stt_model", {"Ses tanıma modeli", "Speech model"}},
        {"set_mic_device", {"Mikrofon", "Microphone"}},
        {"set_default_device", {"Varsayılan", "Default"}},
        {"set_sensitivity", {"Hassasiyet", "Sensitivity"}},
        {"set_follow_up", {"Yanıttan sonra dinleme (sn)", "Listen after a reply (s)"}},
        {"set_listen_start", {"Açılışta dinlemeye başla", "Start listening on launch"}},
        {"set_facts", {"Hatırlanan bilgiler", "Remembered facts"}},
        {"set_clear_history", {"Konuşma geçmişini sil", "Clear conversation history"}},
        {"set_delete", {"Sil", "Delete"}},
        {"set_save", {"Kaydet", "Save"}},
        {"set_close", {"Kapat", "Close"}},
        {"set_saved", {"Kaydedildi", "Saved"}},
        {"set_model_present", {"indirildi", "downloaded"}},
        {"set_model_missing", {"ilk kullanımda indirilecek", "downloads on first use"}},
        {"set_testing", {"Deneniyor…", "Testing…"}},
        {"set_voice_sample", {"Merhaba {sir}, ben Kayra. Sesim böyle duyuluyor.", "Hello {sir}, I am Kayra. This is how I sound."}},
        {"set_no_facts", {"Henüz hatırlanan bir bilgi yok. “Kayra, şunu hatırla: ...” diyebilirsiniz.",
                          "Nothing remembered yet. Say “Kayra, remember that ...”."}},
        {"set_auto_voice", {"Otomatik (dile göre)", "Automatic (by language)"}},
        {"set_key_hint", {"Yerel sağlayıcılar için gerekmez", "Not needed for local providers"}},
        {"set_preset_hint", {"Hazır ayar seçmek adresi ve modeli doldurur.", "Picking a preset fills in the address and model."}},
        {"set_history_cleared", {"Geçmiş silindi", "History cleared"}},
        {"set_hotkey_note", {"Kısayol: Ctrl+Shift+K her yerden dinlemeyi başlatır, Kayra konuşuyorsa susturur.",
                             "Hotkey: Ctrl+Shift+K starts listening from anywhere and silences Kayra while it speaks."}},
    };
    return t;
}
// clang-format on

std::atomic<bool> g_tr{true};
std::mutex g_mu;
std::string g_address = "efendim";

std::string apply(std::string s) {
    std::string addr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        addr = g_address;
    }
    s = replace_all(s, "{Sir}", capitalize(addr));
    s = replace_all(s, "{sir}", addr);
    return s;
}

}  // namespace

void set_language(bool turkish) { g_tr = turkish; }
bool is_turkish() { return g_tr; }

void set_address(const std::string& address) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_address = address;
}

bool has_key(const char* key) { return table().count(key) > 0; }

std::string T(const char* key) {
    auto it = table().find(key);
    if (it == table().end()) {
        log(std::string("missing text: ") + key);
        return key;
    }
    return apply(g_tr ? it->second.tr : it->second.en);
}

std::string F(const char* key, std::initializer_list<std::string> args) {
    std::string s = T(key);
    int i = 0;
    for (auto& a : args) s = replace_all(s, "{" + std::to_string(i++) + "}", a);
    return s;
}

const char* U(const char* key) {
    static std::mutex mu;
    static std::map<std::string, std::string> cache;
    std::lock_guard<std::mutex> lk(mu);
    std::string k = std::string(g_tr ? "tr:" : "en:") + key;
    auto it = cache.find(k);
    if (it == cache.end()) it = cache.emplace(k, T(key)).first;
    return it->second.c_str();
}

std::string weekday_name(int d) {
    static const char* tr[] = {"Pazar", "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi"};
    static const char* en[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    d = ((d % 7) + 7) % 7;
    return g_tr ? tr[d] : en[d];
}

std::string month_name(int m) {
    static const char* tr[] = {"Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
                               "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık"};
    static const char* en[] = {"January", "February", "March", "April", "May", "June",
                               "July", "August", "September", "October", "November", "December"};
    m = ((m % 12) + 12) % 12;
    return g_tr ? tr[m] : en[m];
}

std::string duration_text(double seconds) {
    long long s = (long long)std::llround(seconds);
    long long h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    bool tr = g_tr;
    auto part = [&](long long n, const char* trw, const char* en1, const char* enn) {
        return std::to_string(n) + " " + (tr ? trw : (n == 1 ? en1 : enn));
    };
    std::string out;
    auto add = [&](const std::string& p) { out += (out.empty() ? "" : (tr ? " " : " ")) + p; };
    if (h) add(part(h, "saat", "hour", "hours"));
    if (m) add(part(m, "dakika", "minute", "minutes"));
    if (sec || out.empty()) add(part(sec, "saniye", "second", "seconds"));
    return out;
}

std::string tr_locative(const std::string& place) {
    if (place.empty()) return place;
    std::wstring w = widen(lower(place));
    wchar_t vowel = L'a';
    for (auto it = w.rbegin(); it != w.rend(); ++it) {
        wchar_t c = *it;
        if (c == L'a' || c == L'\u0131' || c == L'o' || c == L'u') {
            vowel = L'a';
            break;
        }
        if (c == L'e' || c == L'i' || c == 0xF6 || c == 0xFC) {
            vowel = L'e';
            break;
        }
    }
    wchar_t last = w.back();
    bool hard = wcschr(L"fstk\u00e7\u015fhp", last) != nullptr;
    std::string suffix = std::string(hard ? "t" : "d") + (vowel == L'a' ? "a" : "e");
    return place + "'" + suffix;
}

}  // namespace kayra
