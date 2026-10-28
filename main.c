/*
  Programlama Lab. I - Proje 1
 * Teknik destek gorevlerinin 4 teknisyen tarafindan yurutulmesi simulasyonu
 * 250201144_Alina Maksatbek kyzy
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#define TEKNISYEN_SAYISI   4
#define KONUM_SAYISI    5
#define EN_FAZLA_GOREV  20000
#define SATIR_UZUNLUGU   1024
#define TUR_UZUNLUGU   39
#define EN_BUYUK_SAYI  1000000000L
#define SAYFA_BOYUTU  20

typedef enum { HENUZ_GELMEDI = 0, BEKLIYOR, CALISIYOR, BITTI } Durum;
static const char *DURUM_ADLARI[] = { "Henuz gelmedi", "Bekliyor", "Devam ediyor", "Tamamlandi" };
static const char *KONUM_ADLARI[KONUM_SAYISI] = { "LAB_A", "LAB_B", "DERSLIK_1", "DERSLIK_2", "DERSLIK_3" };

enum { ALGORITMA_YOK = 0, KISA_IS = 1, ONCELIK = 2, ROUND_ROBIN = 3, KENDI_ALGORITMAM = 4 };
static const char *ALGORITMA_ADLARI[] = { "Secilmedi", "Shortest Job", "Priority", "Round Robin",
                                   "Birlesik SJ+P (Kisa ve Onemli Once)" };

typedef struct {
    int id, gelis, sure, oncelik, konum;
    char type[TUR_UZUNLUGU + 1];
    int kalan;
    Durum durum;
    int teknisyenler;          
    int ilk_baslama;   
    int bitis;        
    int bitiren;      
} Gorev;

typedef struct {
    int mesgul;
    int gorev;             
    int gecis_kalan;        /* kalan gecis suresi (0 veya 1) */
    int son_konum;          /* son bulundugu konum, -1: henuz yok */
    int dilim_kalan;        /* Round Robin: dilimde kalan sure */
    long long calisilan;      /* calisilan toplam birim */
    long long toplam_gecis; /* gecis icin harcanan toplam birim */
} Teknisyen;

//genel durum
static Gorev *gorevler = NULL;
static int gorev_sayisi = 0;
static int *gelis_sirasi = NULL;   /* gorevler (gelis, ID) sirasinda */
static int gelis_indeksi = 0;
static int *kuyruk = NULL;       /* Round Robin ortak kuyrugu (dairesel) */
static int kuyruk_basi = 0, kuyruk_sayisi = 0, kuyruk_kapasitesi = 1;
static Teknisyen teknisyenler[TEKNISYEN_SAYISI];
static long long zaman = 0;
static int algoritma = ALGORITMA_YOK;
static int quantum = 0;
static int gecis_aktif = 1;
static int bekleyen_sayisi = 0, biten_sayisi = 0;

//1. KULLANICIDAN BILGI ALMA
/* Bu bolum, yanlis sayi girilmesini ve cok uzun satirlari kontrol eder. */

static int satir_oku(char *buf, size_t n, const char *prompt)
{
    if (prompt) { printf("%s", prompt); fflush(stdout); }
    if (!fgets(buf, (int)n, stdin)) return 0;
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[--len] = '\0';
    else { int c; while ((c = getchar()) != '\n' && c != EOF) { } }
    if (len > 0 && buf[len - 1] == '\r') buf[len - 1] = '\0';
    return 1;
}

static int sayi_kontrol_et(const char *s, long lo, long hi, long *out)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE) return 0;
    if (v < lo || v > hi) return 0;
    *out = v;
    return 1;
}

/* 1: tamam, 0: EOF, -1: gecersiz (mesaj yazilir) */
static int sayi_iste(const char *prompt, long lo, long hi, long *out)
{
    char buf[128];
    if (!satir_oku(buf, sizeof buf, prompt)) return 0;
    if (!sayi_kontrol_et(buf, lo, hi, out)) {
        printf("Gecersiz giris: %ld ile %ld arasinda bir tam sayi giriniz.\n", lo, hi);
        return -1;
    }
    return 1;
}

//2. DOSYAYI OKUMA VE HATALARI BULMA
/* Hatali gorevler reddedilir, dogru gorevler saklanir. */

static int reddedilen_sayisi = 0;

static void satir_reddet(int lineno, const char *reason)
{
    printf("  [HATA] Satir %d reddedildi: %s\n", lineno, reason);
    reddedilen_sayisi++;
}

static int id_karsilastir(const void *a, const void *b)
{
    const Gorev *x = a, *y = b;
    return (x->id > y->id) - (x->id < y->id);
}

static int gelis_karsilastir(const void *a, const void *b)
{
    const Gorev *x = &gorevler[*(const int *)a], *y = &gorevler[*(const int *)b];
    if (x->gelis != y->gelis) return (x->gelis > y->gelis) - (x->gelis < y->gelis);
    return (x->id > y->id) - (x->id < y->id);
}

static void bellegi_temizle(void)
{
    free(gorevler); 
    free(gelis_sirasi); 
    free(kuyruk);
    gorevler = NULL; 
    gelis_sirasi = NULL; 
    kuyruk = NULL; 
    gorev_sayisi = 0;
}

/* -1: dosya acilamadi, aksi halde gecerli gorev sayisi */
static int dosya_oku(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    bellegi_temizle();
    reddedilen_sayisi = 0;
    int cap = 256;
    gorevler = malloc(sizeof(Gorev) * cap);
    if (!gorevler) { fclose(f); printf("Bellek ayrilamadi.\n"); return -1; }

    char line[SATIR_UZUNLUGU];
    int lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        size_t len = strlen(line);
        int truncated = 0;
        if (lineno == 1 && len >= 3 && (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
            memmove(line, line + 3, len - 2);             /* UTF-8 BOM'u at */
        if (len == sizeof line - 1 && line[len - 1] != '\n') {
            int c = fgetc(f);
            if (c != EOF && c != '\n') {
                truncated = 1;
                while (c != '\n' && c != EOF) c = fgetc(f);
            }
        }

        char *tok[8];
        int nt = 0;
        for (char *p = strtok(line, " \t\r\n\v\f"); p; p = strtok(NULL, " \t\r\n\v\f")) {
            if (nt < 8) tok[nt] = p;
            nt++;
        }
        if (nt == 0 || tok[0][0] == '#') continue;       /* bos / aciklama */
        if (truncated) { satir_reddet(lineno, "satir cok uzun"); continue; }
        if (nt < 6) { satir_reddet(lineno, "eksik alan (6 alan gerekli)"); continue; }
        if (nt > 6) { satir_reddet(lineno, "fazla alan (6 alan gerekli)"); continue; }

        long id, arr, dur, pri;
        if (!sayi_kontrol_et(tok[0], 1, EN_BUYUK_SAYI, &id))  { satir_reddet(lineno, "ID pozitif tam sayi olmali"); continue; }
        if (!sayi_kontrol_et(tok[1], 0, EN_BUYUK_SAYI, &arr)) { satir_reddet(lineno, "GELIS_ZAMANI sifir veya daha buyuk tam sayi olmali"); continue; }
        if (!sayi_kontrol_et(tok[2], 1, EN_BUYUK_SAYI, &dur)) { satir_reddet(lineno, "SURE sifirdan buyuk tam sayi olmali"); continue; }
        if (!sayi_kontrol_et(tok[3], 1, 5, &pri))         { satir_reddet(lineno, "ONCELIK 1-5 arasinda olmali"); continue; }
        if (strlen(tok[4]) > TUR_UZUNLUGU)              { satir_reddet(lineno, "GOREV_TURU 39 karakteri asiyor"); continue; }
        int konum = -1;
        for (int i = 0; i < KONUM_SAYISI; i++) if (strcmp(tok[5], KONUM_ADLARI[i]) == 0) konum = i;
        if (konum < 0) { satir_reddet(lineno, "KONUM gecersiz (LAB_A, LAB_B, DERSLIK_1, DERSLIK_2, DERSLIK_3)"); continue; }

        int dup = 0;
        for (int i = 0; i < gorev_sayisi; i++) 
        if (gorevler[i].id == id) { 
            dup = 1; 
            break; }
        if (dup) { 
            satir_reddet(lineno, "yinelenen ID (ilk gecerli kayit korundu)"); continue; }

        if (gorev_sayisi >= EN_FAZLA_GOREV) { satir_reddet(lineno, "kapasite asildi"); continue; }
        if (gorev_sayisi == cap) {
            cap *= 2;
            Gorev *nt2 = realloc(gorevler, sizeof(Gorev) * cap);
            if (!nt2) { satir_reddet(lineno, "bellek yetersiz"); cap /= 2; continue; }
            gorevler = nt2;
        }
        Gorev *t = &gorevler[gorev_sayisi++];
        memset(t, 0, sizeof *t);
        t->id = (int)id; t->gelis = (int)arr; t->sure = (int)dur;
        t->oncelik = (int)pri; t->konum = konum;
        strcpy(t->type, tok[4]);
    }
    fclose(f);

    qsort(gorevler, gorev_sayisi, sizeof(Gorev), id_karsilastir);
    gelis_sirasi = malloc(sizeof(int) * (gorev_sayisi > 0 ? gorev_sayisi : 1));
    kuyruk_kapasitesi = gorev_sayisi > 0 ? gorev_sayisi : 1;
    kuyruk = malloc(sizeof(int) * kuyruk_kapasitesi);
    if (!gelis_sirasi || !kuyruk) { printf("Bellek ayrilamadi.\n"); bellegi_temizle(); return -1; }
    for (int i = 0; i < gorev_sayisi; i++) gelis_sirasi[i] = i;
    qsort(gelis_sirasi, gorev_sayisi, sizeof(int), gelis_karsilastir);
    return gorev_sayisi;
}

//3. GOREVLERI DAGITMA VE ZAMANI ILERLETME(ALGORITMALAR)

static void kuyruga_ekle(int ti) { 
    kuyruk[(kuyruk_basi + kuyruk_sayisi) % kuyruk_kapasitesi] = ti; 
    kuyruk_sayisi++; }
static int  kuyruktan_al(void)    { 
    int ti = kuyruk[kuyruk_basi]; 
    kuyruk_basi = (kuyruk_basi + 1) % kuyruk_kapasitesi; 
    kuyruk_sayisi--; 
    return ti; }

static void gorev_ata(int ti, int k)
{
    Gorev *t = &gorevler[ti];
    Teknisyen *T = &teknisyenler[k];
    T->mesgul = 1;
    T->gorev = ti;
    t->durum = CALISIYOR;
    t->teknisyenler = k;
    bekleyen_sayisi--;
    if (t->ilk_baslama < 0) t->ilk_baslama = (int)zaman;
    T->gecis_kalan = (gecis_aktif && T->son_konum >= 0 && T->son_konum != t->konum) ? 1 : 0;
    T->son_konum = t->konum;
    T->dilim_kalan = (algoritma == ROUND_ROBIN) ? quantum : 0;
}

static void gelen_gorevleri_ekle(void)
{
    while (gelis_indeksi < gorev_sayisi && gorevler[gelis_sirasi[gelis_indeksi]].gelis <= zaman) {
        int ti = gelis_sirasi[gelis_indeksi++];
        gorevler[ti].durum = BEKLIYOR;
        bekleyen_sayisi++;
        if (algoritma == ROUND_ROBIN) kuyruga_ekle(ti);
    }
}

/*Shortest Job / Priority secimi*/
static int daha_iyi_mi(int a, int b)   /* a, b'den once mi secilmeli? */
{
    const Gorev *x = &gorevler[a], *y = &gorevler[b];
    if (algoritma == KISA_IS && x->sure != y->sure) {
        return x->sure < y->sure;
    }
    if (algoritma == ONCELIK && x->oncelik != y->oncelik) {
        return x->oncelik < y->oncelik;
    }
    if (x->gelis != y->gelis) {
        return x->gelis < y->gelis;
    }
    return x->id < y->id;
}

static void normal_atama(void)
{
    for (int k = 0; k < TEKNISYEN_SAYISI; k++) {
        if (teknisyenler[k].mesgul) continue;
        int best = -1;
        for (int i = 0; i < gorev_sayisi; i++)
            if (gorevler[i].durum == BEKLIYOR && (best < 0 || daha_iyi_mi(i, best))) best = i;
        if (best < 0) return;
        gorev_ata(best, k);
    }
}

static void rr_atama(void)
{
    for (int k = 0; k < TEKNISYEN_SAYISI && kuyruk_sayisi > 0; k++)
        if (!teknisyenler[k].mesgul) gorev_ata(kuyruktan_al(), k);
}

static int gecis_maliyeti(int ti, int k)   /* teknisyen k icin gecis maliyeti: 0 veya 1 */
{
    return (gecis_aktif && teknisyenler[k].son_konum >= 0 && teknisyenler[k].son_konum != gorevler[ti].konum) ? 1 : 0;
}
//4. algoritma(SH+P)
static int ikili_daha_iyi_mi(int ti, int k, int bt, int bk)
{
    const Gorev *x = &gorevler[ti], *y = &gorevler[bt];
    long sx = (long)(x->sure + gecis_maliyeti(ti, k)) * x->oncelik;
    long sy = (long)(y->sure + gecis_maliyeti(bt, bk)) * y->oncelik;
    if (sx != sy) return sx < sy;          /* en dusuk puan */
    if (x->sure != y->sure) return x->sure < y->sure; /* kisa gorev */
    if (x->oncelik != y->oncelik) return x->oncelik < y->oncelik;
    if (x->gelis != y->gelis) return x->gelis < y->gelis;
    if (x->id != y->id) return x->id < y->id;
    return k < bk;
}

/* Bos teknisyen ve bekleyen gorevleri tek tek karsilastir.
 * En kucuk puani veren teknisyen-gorev ikilisini sec. */
static void ozel_atama(void)
{
    while (bekleyen_sayisi > 0) {
        int bt = -1, bk = -1;
        for (int k = 0; k < TEKNISYEN_SAYISI; k++) {
            if (teknisyenler[k].mesgul) continue;
            for (int i = 0; i < gorev_sayisi; i++) {
                if (gorevler[i].durum != BEKLIYOR) continue;
                if (bt < 0 || ikili_daha_iyi_mi(i, k, bt, bk)) { bt = i; bk = k; }
            }
        }
        if (bt < 0) return;           /* bos teknisyen kalmadi */
        gorev_ata(bt, bk);
    }
}

static void atamalari_yap(void)
{
    switch (algoritma) {
    case KISA_IS: case ONCELIK: normal_atama(); break;
    case ROUND_ROBIN:                 rr_atama(); break;
    case KENDI_ALGORITMAM:             ozel_atama(); break;
    default: break;
    }
}

static void simulasyonu_sifirla(void)
{
    for (int i = 0; i < gorev_sayisi; i++) {
        Gorev *t = &gorevler[i];
        t->kalan = t->sure;
        t->durum = HENUZ_GELMEDI;
        t->teknisyenler = -1; t->ilk_baslama = -1; t->bitis = -1; t->bitiren = -1;
    }
    for (int k = 0; k < TEKNISYEN_SAYISI; k++) {
        memset(&teknisyenler[k], 0, sizeof(Teknisyen));
        teknisyenler[k].gorev = -1; teknisyenler[k].son_konum = -1;
    }
    zaman = 0; gelis_indeksi = 0; kuyruk_basi = 0; kuyruk_sayisi = 0;
    bekleyen_sayisi = 0; biten_sayisi = 0;
    gelen_gorevleri_ekle();
    atamalari_yap();
}

static int herkes_bos_mu(void)
{
    for (int k = 0; k < TEKNISYEN_SAYISI; k++) 
    if (teknisyenler[k].mesgul) return 0;
    return 1;
}

/* Tam olarak 1 zaman birimi ilerletir. */
/* Her cagrida saat 1 ilerler: gecis/calisma, bitenler,
 * Round Robin dilimi bitenler, yeni gelenler, yeni atamalar. */
static void bir_birim_ilerle(void)
{
    /* 1) calisma / gecis */
    for (int k = 0; k < TEKNISYEN_SAYISI; k++) {
        Teknisyen *T = &teknisyenler[k];
        if (!T->mesgul) continue;
        if (T->gecis_kalan > 0) { T->gecis_kalan--; T->toplam_gecis++; continue; }
        gorevler[T->gorev].kalan--;
        T->calisilan++;
        if (algoritma == ROUND_ROBIN) T->dilim_kalan--;
    }
    zaman++;

    /* 2) biten gorevler */
    for (int k = 0; k < TEKNISYEN_SAYISI; k++) {
        Teknisyen *T = &teknisyenler[k];
        if (!T->mesgul) continue;
        Gorev *t = &gorevler[T->gorev];
        if (t->kalan == 0) {
            t->durum = BITTI; t->bitis = (int)zaman; t->bitiren = k; t->teknisyenler = -1;
            T->mesgul = 0; T->gorev = -1; T->dilim_kalan = 0;
            biten_sayisi++;
        }
    }

    /* 3) Round Robin: dilimi dolan fakat bitmeyen gorevler teknisyenden ayrilir */
    int expired[TEKNISYEN_SAYISI], ne = 0;
    if (algoritma == ROUND_ROBIN) {
        for (int k = 0; k < TEKNISYEN_SAYISI; k++) {
            Teknisyen *T = &teknisyenler[k];
            if (T->mesgul && T->gecis_kalan == 0 && T->dilim_kalan == 0) {
                Gorev *t = &gorevler[T->gorev];
                t->durum = BEKLIYOR; t->teknisyenler = -1;
                bekleyen_sayisi++;
                expired[ne++] = T->gorev;
                T->mesgul = 0; T->gorev = -1;
            }
        }
    }

    /* 4) yeni gelisler (kucuk ID) ve ardindan dilimi dolanlar (teknisyen sirasi) */
    gelen_gorevleri_ekle();
    for (int i = 0; i < ne; i++) kuyruga_ekle(expired[i]);

    /* 5) bos teknisyenlere atama */
    atamalari_yap();
}

/* Bos bir sistemde bos yere adim atmamak icin gelecek ilk gelise atlar. */
static void ilerle_veya_atla(long long limit)
{
    if (herkes_bos_mu() && bekleyen_sayisi == 0) {
        if (gelis_indeksi >= gorev_sayisi) { zaman = limit; return; }
        long long na = gorevler[gelis_sirasi[gelis_indeksi]].gelis;
        if (na > limit) { zaman = limit; return; }
        zaman = na;
        gelen_gorevleri_ekle();
        atamalari_yap();
        return;
    }
    bir_birim_ilerle();
}

static void hedefe_git(long long target)
{
    if (target < zaman) simulasyonu_sifirla();          /* gecmise donus: bastan yeniden hesapla */
    while (zaman < target) ilerle_veya_atla(target);
}

static void sonuna_kadar_calistir(void)
{
    while (biten_sayisi < gorev_sayisi) ilerle_veya_atla(LLONG_MAX);
}

//4. SONUCLARI HESAPLAMA

typedef struct {
    int tamamlanan;
    double ort_bekleme, ort_sistem, genel_kullanim;
    double kullanim[TEKNISYEN_SAYISI];
    long long son_bitis;      /* tum gorevler bittiyse */
    int hepsi_bitti;
    long long en_son_bitis;
    long long toplam_calisma, toplam_gecis;
} Istatistik;

static void istatistik_hesapla(Istatistik *s)
{
    memset(s, 0, sizeof *s);
    double sw = 0, ss = 0;
    for (int i = 0; i < gorev_sayisi; i++) {
        const Gorev *t = &gorevler[i];
        if (t->durum != BITTI) continue;
        s->tamamlanan++;
        sw += (double)t->bitis - t->gelis - t->sure;
        ss += (double)t->bitis - t->gelis;
        if (t->bitis > s->en_son_bitis) s->en_son_bitis = t->bitis;
    }
    if (s->tamamlanan > 0) { 
        s->ort_bekleme = sw / s->tamamlanan; 
        s->ort_sistem = ss / s->tamamlanan; }
    s->hepsi_bitti = (gorev_sayisi > 0 && s->tamamlanan == gorev_sayisi);
    if (s->hepsi_bitti) 
        s->son_bitis = s->en_son_bitis;
    for (int k = 0; k < TEKNISYEN_SAYISI; k++) {
        s->toplam_calisma += teknisyenler[k].calisilan;
        s->toplam_gecis += teknisyenler[k].toplam_gecis;
        if (zaman > 0) s->kullanim[k] = 100.0 * teknisyenler[k].calisilan / (double)zaman;
    }
    if (zaman > 0) s->genel_kullanim = 100.0 * s->toplam_calisma / (4.0 * (double)zaman);
}

static void istatistik_goster(void)
{
    Istatistik s;
    istatistik_hesapla(&s);
    int cnt[4] = { 0, 0, 0, 0 };
    for (int i = 0; i < gorev_sayisi; i++) cnt[gorevler[i].durum]++;

    printf("\n--- ISTATISTIKLER (zaman %lld, %s", zaman, ALGORITMA_ADLARI[algoritma]);
    if (algoritma == ROUND_ROBIN) printf(", quantum=%d", quantum);
    printf(") ---\n");
    printf("Toplam gecerli gorev : %d\n", gorev_sayisi);
    for (int i = 0; i < 4; i++) printf("  %-14s: %d\n", DURUM_ADLARI[i], cnt[i]);
    printf("  (toplam: %d)\n", cnt[0] + cnt[1] + cnt[2] + cnt[3]);

    if (s.hepsi_bitti) printf("Son bitis zamani (son_bitis) : %lld\n", s.son_bitis);
    else if (gorev_sayisi == 0) printf("Son bitis zamani (son_bitis) : hesaplanamaz (gorev yok)\n");
    else printf("Son bitis zamani (son_bitis) : henuz tum gorevler tamamlanmadi\n");

    if (s.tamamlanan > 0) {
        printf("Ortalama bekleme suresi     : %.3f\n", s.ort_bekleme);
        printf("Ortalama sistemde kalma     : %.3f\n", s.ort_sistem);
    } else {
        printf("Ortalama bekleme suresi     : hesaplanamaz\n");
        printf("Ortalama sistemde kalma     : hesaplanamaz\n");
    }
    if (zaman > 0) {
        for (int k = 0; k < TEKNISYEN_SAYISI; k++)
            printf("T%d kullanim orani           : %%%.3f  (calisilan %lld birim)\n",
                   k + 1, s.kullanim[k], teknisyenler[k].calisilan);
        printf("Genel kullanim orani        : %%%.3f\n", s.genel_kullanim);
    } else {
        printf("Kullanim oranlari           : hesaplanamaz (zaman 0)\n");
    }
    printf("Konum degisimi icin harcanan toplam gecis suresi: %lld birim\n", s.toplam_gecis);
}

//5. EKRANDA GOSTERME 

static void tablo_basligi(void)
{
    printf("%-7s %-6s %-6s %-8s %-14s %-10s %-6s\n",
           "ID", "Gelis", "Sure", "Oncelik", "Durum", "Teknisyen", "Kalan");
}

static void gorev_satiri(const Gorev *t)
{
    char tb[16];
    if (t->durum == CALISIYOR) snprintf(tb, sizeof tb, "T%d", t->teknisyenler + 1);
    else strcpy(tb, "-");
    printf("%-7d %-6d %-6d %-8d %-14s %-10s %-6d\n",
           t->id, t->gelis, t->sure, t->oncelik, DURUM_ADLARI[t->durum], tb, t->kalan);
}

/* Sayfalama: 0 dondurursa kullanici cikmak istedi */
static int sayfa_sor(int shown)
{
    if (shown > 0 && shown % SAYFA_BOYUTU == 0) {
        char buf[64];
        if (!satir_oku(buf, sizeof buf, "-- Enter: sonraki sayfa, q: menuye don -- ")) return 0;
        if (buf[0] == 'q' || buf[0] == 'Q') return 0;
    }
    return 1;
}

static void gorevleri_listele(int filter /* -1: hepsi */)
{
    int shown = 0, total = 0;
    for (int i = 0; i < gorev_sayisi; i++) 
    if (filter < 0 || (int)gorevler[i].durum == filter) total++;
    if (total == 0) { printf("Listelenecek gorev yok.\n"); return; }
    printf("\n%d gorev listeleniyor (ID sirasiyla):\n", total);
    tablo_basligi();
    for (int i = 0; i < gorev_sayisi; i++) {
        if (filter >= 0 && (int)gorevler[i].durum != filter) continue;
        if (!sayfa_sor(shown)) return;
        gorev_satiri(&gorevler[i]);
        shown++;
    }
}

static void duruma_gore_listele(void)
{
    printf("\n1 - %s\n2 - %s\n3 - %s\n4 - %s\n0 - Geri\n",
           DURUM_ADLARI[0], DURUM_ADLARI[1], DURUM_ADLARI[2], DURUM_ADLARI[3]);
    long c;
    if (sayi_iste("Durum seciniz: ", 0, 4, &c) != 1 || c == 0) return;
    gorevleri_listele((int)c - 1);
}

static void gorev_sorgula(void)
{
    long id;
    if (sayi_iste("Gorev ID: ", 1, EN_BUYUK_SAYI, &id) != 1) return;
    int lo = 0, hi = gorev_sayisi - 1, idx = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (gorevler[mid].id == id) { idx = mid; break; }
        if (gorevler[mid].id < id) lo = mid + 1; else hi = mid - 1;
    }
    if (idx < 0) { printf("ID %ld bulunamadi.\n", id); return; }
    const Gorev *t = &gorevler[idx];
    printf("\n--- Gorev %d ---\n", t->id);
    printf("Gorev turu        : %s\n", t->type);
    printf("Konum             : %s\n", KONUM_ADLARI[t->konum]);
    printf("Gelis zamani      : %d\n", t->gelis);
    printf("Toplam sure       : %d\n", t->sure);
    printf("Oncelik           : %d\n", t->oncelik);
    printf("Durum             : %s\n", DURUM_ADLARI[t->durum]);
    if (t->durum == CALISIYOR) printf("Guncel teknisyen  : T%d\n", t->teknisyenler + 1);
    else printf("Guncel teknisyen  : -\n");
    printf("Kalan sure        : %d\n", t->kalan);
    if (t->ilk_baslama >= 0) printf("Ilk baslama       : %d\n", t->ilk_baslama);
    else printf("Ilk baslama       : -\n");
    if (t->durum == BITTI) {
        printf("Tamamlanma zamani : %d\n", t->bitis);
        printf("Toplam bekleme    : %d\n", t->bitis - t->gelis - t->sure);
        printf("Bitiren teknisyen : T%d\n", t->bitiren + 1);
    } else {
        printf("Tamamlanma zamani : -\n");
    }
}

static void teknisyenleri_goster(void)
{
    printf("\n--- TEKNISYENLER (zaman %lld) ---\n", zaman);
    for (int k = 0; k < TEKNISYEN_SAYISI; k++) {
        const Teknisyen *T = &teknisyenler[k];
        printf("T%d: ", k + 1);
        if (!T->mesgul) printf("Bos     | Gorev: -  | Kalan: -");
        else printf("%-7s | Gorev: %-4d| Kalan: %d", T->gecis_kalan ? "Geciste" : "Mesgul",
                    gorevler[T->gorev].id, gorevler[T->gorev].kalan);
        printf(" | Calisilan toplam: %lld", teknisyenler[k].calisilan);
        if (algoritma == ROUND_ROBIN) {
            if (T->mesgul) printf(" | Dilimde kalan: %d", T->dilim_kalan);
            else printf(" | Dilimde kalan: -");
        }
        printf(" | Son konum: %s", T->son_konum >= 0 ? KONUM_ADLARI[T->son_konum] : "-");
        if (zaman > 0) printf(" | Kullanim: %%%.3f", 100.0 * T->calisilan / (double)zaman);
        printf("\n");
    }
}

static void menu_goster(void)
{
    printf("\n==============================================================\n");
    printf(" Guncel zaman: %lld | Algoritma: %s", zaman, ALGORITMA_ADLARI[algoritma]);
    if (algoritma == ROUND_ROBIN) printf(" (quantum=%d)", quantum);
    printf("%s\n", gecis_aktif ? "" : " | Gecis maliyeti: KAPALI");
    printf("==============================================================\n");
    printf(" 1 - Algoritma sec veya degistir\n");
    printf(" 2 - Tum gorevleri listele\n");
    printf(" 3 - Gorevleri durumuna gore listele\n");
    printf(" 4 - Gorev ID ile sorgula\n");
    printf(" 5 - Teknisyenlerin durumunu goster\n");
    printf(" 6 - Zamani 1 birim ilerlet\n");
    printf(" 7 - Zamani N birim ilerlet\n");
    printf(" 8 - Belirli bir zamana git\n");
    printf(" 9 - Istatistikleri goster\n");
    printf("10 - Simulasyonu sifirla\n");
    printf(" 0 - Cikis\n");
}

/* 1: algoritma secildi, 0: iptal/EOF */
static int algoritma_sec(void)
{
    printf("\n1 - Shortest Job\n2 - Priority\n3 - Round Robin\n4 - Kendi Algoritmaniz (%s)\n0 - Geri\n",
           ALGORITMA_ADLARI[KENDI_ALGORITMAM]);
    long c;
    int r = sayi_iste("Algoritma seciniz: ", 0, 4, &c);
    if (r != 1 || c == 0) return 0;
    int q = quantum;
    if (c == 3) {
        long qv;
        if (sayi_iste("Zaman dilimi (quantum, >=1): ", 1, 1000000, &qv) != 1) return 0;
        q = (int)qv;
    }
    algoritma = (int)c;
    quantum = (algoritma == ROUND_ROBIN) ? q : 0;
    simulasyonu_sifirla();
    printf("%s secildi; simulasyon zaman 0'dan baslatildi.\n", ALGORITMA_ADLARI[algoritma]);
    return 1;
}

//6. DORT ALGORITMAYI KARSILASTIRMA

/* Her algoritma icin simulasyonu bastan baslatir,
 * tum gorevleri bitirir ve ayni olculeri ekrana yazar. */
static void algoritmalari_karsilastir(int q)
{
    printf("\n%-28s %9s %12s %12s %10s %8s\n",
           "Algoritma", "Makespan", "Ort.bekleme", "Ort.sistem", "Genel kul.", "Gecis");
    for (int a = KISA_IS; a <= KENDI_ALGORITMAM; a++) {
        algoritma = a; quantum = (a == ROUND_ROBIN) ? q : 0;
        simulasyonu_sifirla();
        sonuna_kadar_calistir();
        Istatistik s; istatistik_hesapla(&s);
        char name[48];
        if (a == ROUND_ROBIN) snprintf(name, sizeof name, "Round Robin (q=%d)", q);
        else snprintf(name, sizeof name, "%s", ALGORITMA_ADLARI[a]);
        printf("%-28s %9lld %12.3f %12.3f %9.3f%% %8lld\n", name, s.son_bitis, s.ort_bekleme,
               s.ort_sistem, s.genel_kullanim, s.toplam_gecis);
    }
}

/* Yolun basindaki/sonundaki bosluk ve tirnaklari siler
 * (Windows'ta "Yol olarak kopyala" yolu tirnakli verir). */
static void yolu_duzelt(char *p)
{
    size_t n = strlen(p), a = 0;
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '"' || p[n - 1] == '\'')) n--;
    while (a < n && (p[a] == ' ' || p[a] == '\t' || p[a] == '"' || p[a] == '\'')) a++;
    memmove(p, p + a, n - a);
    p[n - a] = '\0';
}

//7. PROGRAMIN BASLANGICI

int main(int argc, char **argv)
{
    const char *cmp_path = NULL;
    int cmp_q = 2;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gecissiz") == 0) gecis_aktif = 0;
        else if (strcmp(argv[i], "--karsilastir") == 0 && i + 1 < argc) {
            cmp_path = argv[++i];
            long qv;
            if (i + 1 < argc && sayi_kontrol_et(argv[i + 1], 1, 1000000, &qv)) { 
                cmp_q = (int)qv; i++; 
            }
        }
    }

    if (cmp_path) {
        int n = dosya_oku(cmp_path);
        if (n < 0) 
        { printf("Dosya acilamadi: %s\n", cmp_path); 
          return 1; }
        printf("%d gecerli gorev, %d satir reddedildi.%s\n", n, reddedilen_sayisi,
               gecis_aktif ? "" : " (gecis maliyeti kapali)");
        algoritmalari_karsilastir(cmp_q);
        bellegi_temizle();
        return 0;
    }

    /* dosya yolu kullanicidan alinir */
    for (;;) {
        char path[512];
        if (!satir_oku(path, sizeof path, "Gorev dosyasinin yolu (bos giris: cikis): ")) return 0;
        yolu_duzelt(path);
        if (path[0] == '\0') return 0;
        int n = dosya_oku(path);
        if (n < 0) { printf("HATA: '%s' dosyasi acilamadi veya bellek ayrilamadi.\n", path); continue; }
        printf("%d gecerli gorev okundu, %d satir reddedildi.\n", n, reddedilen_sayisi);
        if (n == 0) printf("UYARI: gecerli gorev yok; simulasyon bos calisacak.\n");
        break;
    }

    while (!algoritma_sec()) {
        if (feof(stdin)) { 
            bellegi_temizle(); 
            return 0; }
    }

    for (;;) {
        menu_goster();
        long c;
        int r = sayi_iste("Seciminiz: ", 0, 10, &c);
        if (r == 0) break;
        if (r < 0) continue;
        long v;
        switch (c) {
        case 0: bellegi_temizle(); printf("Cikis yapiliyor.\n"); return 0;
        case 1: algoritma_sec(); break;
        case 2: gorevleri_listele(-1); break;
        case 3: duruma_gore_listele(); break;
        case 4: gorev_sorgula(); break;
        case 5: teknisyenleri_goster(); break;
        case 6: hedefe_git(zaman + 1); printf("Zaman: %lld\n", zaman); break;
        case 7:
            if (sayi_iste("N (pozitif tam sayi): ", 1, EN_BUYUK_SAYI, &v) == 1) {
                hedefe_git(zaman + v); printf("Zaman: %lld\n", zaman);
            }
            break;
        case 8:
            if (sayi_iste("Hedef zaman (>=0): ", 0, EN_BUYUK_SAYI, &v) == 1) {
                hedefe_git(v); printf("Zaman: %lld\n", zaman);
            }
            break;
        case 9: istatistik_goster(); break;
        case 10: simulasyonu_sifirla(); printf("Simulasyon sifirlandi (zaman 0, ilk atamalar yapildi).\n"); break;
        }
    }
    bellegi_temizle();
    return 0;
}