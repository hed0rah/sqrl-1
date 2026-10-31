/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 databurn
 * lts: the NRL letter-to-sound rules (Elovitz, Johnson, McHugh and Shore 1976, NRL Report 7948),
 * written out again for MOUTH's phonemes: the report's AX is AH here and WH is W, as MOUTH has
 * neither. Rules with an apostrophe are left out (words here are letters only).
 *
 * A rule is "left[match]right=phonemes". The context symbols, as the report defines them:
 *   ' '  a word boundary          #  one or more vowels (A E I O U)
 *   :    zero or more consonants   ^  one consonant
 *   .    one voiced consonant (B D V G J L M N R W Z)
 *   +    a front vowel (E I Y)     %  a suffix: ER E ES ED ING ELY (right only)
 *   &    a sibilant (S C G Z X J CH SH)
 *   @    a consonant that makes a long U "oo" (T S R D L Z N J TH CH SH)
 * The rules of a letter are tried in order and the first that fits is used. */
#include "lts.h"

static const char *const LTS_RULES[] = {
    /* A */
    "[A] =AH", " [ARE] =AA R", " [AR]O=AH R", "[AR]#=EH R", " ^[AS]#=EY S", "[A]WA=AH", "W[A]SH=AA", "[AW]=AO",
    " :[ANY]=EH N IY", "[A]^+#=EY", "#:[ALLY]=AH L IY", " [AL]#=AH L", "[AGAIN]=AH G EH N", "#:[AG]E=IH JH",
    "[A]^+:#=AE", " :[A]^+ =EY", "[A]^%=EY", " [ARR]=AH R", "[ARR]=AE R", " :[AR] =AA R", "[AR] =ER",
    "[AR]=AA R", "[AIR]=EH R", "[AI]=EY", "[AY]=EY", "[AU]=AO", "#:[AL] =AH L", "#:[ALS] =AH L Z", "[ALK]=AO K",
    "[AL]^=AO L", " :[ABLE]=EY B AH L", "[ABLE]=AH B AH L", "[ANG]+=EY N JH", "[A]=AE",
    /* B */
    " [BE]^#=B IH", "[BEING]=B IY IH NG", " [BOTH] =B OW TH", " [BUS]#=B IH Z", "[BUIL]=B IH L", "[B]=B",
    /* C */
    " [CH]^=K", "^E[CH]=K", "[CH]=CH", " S[CI]#=S AY", "[CI]A=SH", "[CI]O=SH", "[CI]EN=SH", "[C]+=S", "[CK]=K",
    "[COM]%=K AH M", "[C]=K",
    /* D */
    "#:[DED] =D IH D", ".E[D] =D", "#:^E[D] =T", " [DE]^#=D IH", " [DO] =D UW", " [DOES]=D AH Z",
    " [DOING]=D UW IH NG", " [DOW]=D AW", "[DU]A=JH UW", "[D]=D",
    /* E */
    "#:[E] =", " :[E] =IY", "#[ED] =D", "#:[E]D =", "[EV]ER=EH V", "[E]^%=IY", "[ERI]#=IY R IY", "[ERI]=EH R IH",
    "#:[ER]#=ER", "[ER]#=EH R", "[ER]=ER", " [EVEN]=IY V EH N", "#:[E]W=", "@[EW]=UW", "[EW]=Y UW", "[E]O=IY",
    "#:&[ES] =IH Z", "#:[E]S =", "#:[ELY] =L IY", "#:[EMENT]=M EH N T", "[EFUL]=F UH L", "[EE]=IY", "[EARN]=ER N",
    " [EAR]^=ER", "[EAD]=EH D", "H[EA]V=EH", "#:[EA] =IY AH", "[EA]SU=EH", "[EA]=IY", "[EIGH]=EY", "[EI]=IY", " [EYE]=AY",
    "[EY]=IY", "[EU]=Y UW", "#:^[E]N =AH", "[E]=EH",
    /* F */
    "[FUL]=F UH L", " [FROM] =F R AH M", "[F]=F",
    /* G */
    "[GIV]=G IH V", " [G]I^=G", "[GE]T=G EH", "SU[GGES]=G JH EH S", "[GG]=G", " B#[G]=G", "[G]+=JH",
    "[GREAT]=G R EY T", "#[GH]=", "[G]=G",
    /* H */
    " [HAV]=HH AE V", " [HERE]=HH IY R", " [HOUR]=AW ER", "[HOW]=HH AW", "[H]#=HH", "[H]=",
    /* I */
    " [IN]=IH N", " [I] =AY", "[IN]D=AY N", "[IER]=IY ER", "#:R[IED] =IY D", "[IED] =AY D", "[IEN]=IY EH N",
    "[IE]T=AY EH", " :[I]%=AY", "[I]%=IY", "[IE]=IY", "[I]^+:#=IH", "[IR]#=AY R", "[IZ]%=AY Z", "[IS]%=AY Z",
    "[I]D%=AY", "CH[I]LDR=IH", "+^[I]^+=IH", "[I]T%=AY", "#:^[I]^+=IH", "[I]^+=AY", "[IR]=ER", "[IGH]=AY", "[ILD]=AY L D",
    "[IGN] =AY N", "[IGN]^=AY N", "[IGN]%=AY N", "[IQUE]=IY K", "[I]=IH",
    /* J K L M N */
    "[J]=JH",
    " [K]N=", "[K]=K",
    "[LO]C#=L OW", "L[L]=", "#:^[L]%=AH L", "[LEAD]=L IY D", "[L]=L",
    "[MOV]=M UW V", "[M]=M",
    "E[NG]+=N JH", "[NG]R=NG G", "[NG]#=NG G", "[NGL]%=NG G AH L", "[NG]=NG", "[NK]=NG K", " [NOW] =N AW", "[N]=N",
    /* O */
    "[OF] =AH V", "[OROUGH]=ER OW", "#:[OR] =ER", "#:[ORS] =ER Z", "[OR]=AO R", " [ONE]=W AH N", "[OW]=OW",
    " [OVER]=OW V ER", "[OV]=AH V", "[O]^%=OW", "[O]^EN=OW", "[O]^I#=OW", "[OL]D=OW L", "[OUGHT]=AO T",
    "[OUGH]=AH F", " [OU]=AW", "H[OU]S#=AW S", "[OUS]=AH S", "[OUR]=AO R", "[OULD]=UH D", "^[OU]^L=AH",
    "[OUP]=UW P", "[OU]=AW", "[OY]=OY", "[OING]=OW IH NG", "[OI]=OY", "[OOR]=AO R", "[OOK]=UH K", "[OOD]=UH D",
    "[OO]=UW", "[O]E=OW", "[O] =OW", "[OA]=OW", " [ONLY]=OW N L IY", " [ONCE]=W AH N S", "C[O]N=AA", "[O]NG=AO",
    " :^[O]N=AH", "I[ON]=AH N", "#:[ON] =AH N", "#^[ON]=AH N", "[O]ST =OW", "[OF]^=AO F", "[OTHER]=AH DH ER",
    "[OSS] =AO S", "#:^[OM]=AH M", "[O]=AA",
    /* P Q R */
    "[PH]=F", "[PEOP]=P IY P", "[POW]=P AW", "[PUT] =P UH T", "[P]=P",
    "[QUAR]=K W AO R", "[QU]=K W", "[Q]=K",
    " [RE]^#=R IY", " [ROBOT]=R OW B AA T", "[R]=R",
    /* S */
    "[SH]=SH", "#[SION]=ZH AH N", "[SOME]=S AH M", "#[SUR]#=ZH ER", "[SUR]#=SH ER", "#[SU]#=ZH UW",
    "#[SSU]#=SH UW", "#[SED] =Z D", "#[S]#=Z", "[SAID]=S EH D", "^[SION]=SH AH N", "[S]S=", ".[S] =Z",
    "#:.E[S] =Z", "#:^##[S] =Z", "#:^#[S] =S", "U[S] =S", " :#[S] =Z", " [SCH]=S K", "[S]C+=", "#[SM]=Z M",
    "[S]=S",
    /* T */
    " [THE] =DH AH", "[TO] =T UW", "[THAT] =DH AE T", " [THIS] =DH IH S", " [THEY]=DH EY", " [THERE]=DH EH R",
    "[THER]=DH ER", "[THEIR]=DH EH R", " [THAN] =DH AE N", " [THEM] =DH EH M", "[THESE] =DH IY Z",
    " [THEN]=DH EH N", "[THROUGH]=TH R UW", "[THOSE]=DH OW Z", "[THOUGH] =DH OW", " [THUS]=DH AH S", "[TH]=TH",
    "#:[TED] =T IH D", "S[TI]#N=CH", "[TI]O=SH", "[TI]A=SH", "[TIEN]=SH AH N", "[TUR]#=CH ER", "[TU]A=CH UW",
    " [TWO]=T UW", "[T]=T",
    /* U */
    " [UN]I=Y UW N", " [UN]=AH N", " [UPON]=AH P AO N", "@[UR]#=UH R", "[UR]#=Y UH R", "[UR]^=ER", "[U]^ =AH",
    "[U]^^=AH", "[UY]=AY", " G[U]#=", "G[U]%=", "G[U]#=W", "#N[U]=Y UW", "@[U]=UW", "[U]=Y UW",
    /* V W X Y Z */
    " [VIEW]=V Y UW", "[V]=V",
    " [WERE]=W ER", " [WAS] =W AH Z", "[WAT]ER=W AO T", "[WAT]=W AA T", "[WHERE]=W EH R", " [WHAT]=W AH T", "[WHAT]=W AA T", "[WHOL]=HH OW L",
    "[WHO]=HH UW", "[WH]=W", "[WAR]=W AO R", "[WOR]^=W ER", "[WR]=R", "[W]=W",
    "[X]=K S",
    "[YOUNG]=Y AH NG", " [YOU]=Y UW", " [YES]=Y EH S", " [Y]=Y", "#:^[Y] =IY", "#:^[Y]I=IY", " :[Y] =AY",
    " :[Y]#=AY", " :[Y]^+:#=IH", " :[Y]^#=AY", "[Y]=IH",
    "[Z]=Z",
};
#define LTS_NRULES (sizeof LTS_RULES / sizeof LTS_RULES[0])
#define LTS_MAXW 32

static int lts_letter(char c) { return c >= 'A' && c <= 'Z'; }
static int lts_vowel(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }
static int lts_cons(char c) { return lts_letter(c) && !lts_vowel(c); }
static int lts_in(char c, const char *set)
{
    for (; *set; set++)
        if (*set == c)
            return 1;
    return 0;
}

/* the left context l[0..n) ends at w[p]: walk it backwards; before w[0] is a word boundary */
static int lts_left(const char *l, int n, const char *w, int p)
{
    int j;
    for (j = n - 1; j >= 0; j--) {
        char c = l[j], x = p >= 0 ? w[p] : ' ';
        switch (c) {
        case ' ':
            if (lts_letter(x))
                return 0;
            p--;
            break;
        case '#':
            if (!lts_vowel(x))
                return 0;
            while (p >= 0 && lts_vowel(w[p]))
                p--;
            break;
        case ':':
            while (p >= 0 && lts_cons(w[p]))
                p--;
            break;
        case '^':
            if (!lts_cons(x))
                return 0;
            p--;
            break;
        case '.':
            if (!lts_in(x, "BDVGJLMNRWZ"))
                return 0;
            p--;
            break;
        case '+':
            if (!lts_in(x, "EIY"))
                return 0;
            p--;
            break;
        case '&':
        case '@':
            if (x == 'H' && p > 0 && lts_in(w[p - 1], c == '&' ? "CS" : "TCS"))
                p -= 2;
            else if (lts_in(x, c == '&' ? "SCGZXJ" : "TSRDLZNJ"))
                p--;
            else
                return 0;
            break;
        default:
            if (x != c)
                return 0;
            p--;
        }
    }
    return 1;
}

/* the right context r[0..n) starts at w[p], walking forwards; w[len] onwards is space */
static int lts_right(const char *r, int n, const char *w, int len, int p)
{
    int j;
#define X(i) ((i) < len ? w[i] : ' ')
    for (j = 0; j < n; j++) {
        char c = r[j], x = X(p);
        switch (c) {
        case ' ':
            if (lts_letter(x))
                return 0;
            p++;
            break;
        case '#':
            if (!lts_vowel(x))
                return 0;
            while (lts_vowel(X(p)))
                p++;
            break;
        case ':':
            while (lts_cons(X(p)))
                p++;
            break;
        case '^':
            if (!lts_cons(x))
                return 0;
            p++;
            break;
        case '.':
            if (!lts_in(x, "BDVGJLMNRWZ"))
                return 0;
            p++;
            break;
        case '+':
            if (!lts_in(x, "EIY"))
                return 0;
            p++;
            break;
        case '%':                                        /* ING ELY ER ES ED E, ending the word */
            if (x == 'I' && X(p + 1) == 'N' && X(p + 2) == 'G')
                p += 3;
            else if (x == 'E')
                p += X(p + 1) == 'L' && X(p + 2) == 'Y' ? 3 : lts_in(X(p + 1), "RSD") ? 2 : 1;
            else
                return 0;
            if (lts_letter(X(p)))
                return 0;
            break;
        case '&':
        case '@':
            if (X(p + 1) == 'H' && lts_in(x, c == '&' ? "CS" : "TCS"))
                p += 2;
            else if (lts_in(x, c == '&' ? "SCGZXJ" : "TSRDLZNJ"))
                p++;
            else
                return 0;
            break;
        default:
            if (x != c)
                return 0;
            p++;
        }
    }
#undef X
    return 1;
}

/* the phoneme names in s (spaces between) onto out */
static uint32_t lts_emit(const char *s, uint8_t *out, uint32_t n, uint32_t max)
{
    while (*s && n < max) {
        uint32_t i, k;
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        for (i = 0; s[i] && s[i] != ' '; i++)
            ;
        for (k = 1; k < MTH_NPH; k++) {
            uint32_t j;
            for (j = 0; j < i && MTH_PH_NAMES[k][j] == s[j]; j++)
                ;
            if (j == i && MTH_PH_NAMES[k][i] == 0)
                break;
        }
        if (k < MTH_NPH)
            out[n++] = (uint8_t)k;
        s += i;
    }
    return n;
}

/* one word w[0..len) (upper case letters) */
static uint32_t lts_word(const char *w, int len, uint8_t *out, uint32_t n, uint32_t max)
{
    int p = 0;
    while (p < len) {
        uint32_t r;
        for (r = 0; r < LTS_NRULES; r++) {
            const char *s = LTS_RULES[r], *lb = s, *rb, *eq;
            int ml, k;
            while (*lb != '[')
                lb++;
            if (lb[1] != w[p])
                continue;
            for (rb = lb; *rb != ']'; rb++)
                ;
            for (eq = rb; *eq != '='; eq++)
                ;
            ml = (int)(rb - lb - 1);
            for (k = 0; k < ml && p + k < len && w[p + k] == lb[1 + k]; k++)
                ;
            if (k < ml)
                continue;
            if (!lts_left(s, (int)(lb - s), w, p - 1) || !lts_right(rb + 1, (int)(eq - rb - 1), w, len, p + ml))
                continue;
            n = lts_emit(eq + 1, out, n, max);
            p += ml;
            break;
        }
        if (r == LTS_NRULES)                             /* every letter has a last rule; never here */
            p++;
    }
    return n;
}

uint32_t lts_text(const char *text, uint8_t *out, uint32_t max)
{
    char w[LTS_MAXW];
    uint32_t n = 0;
    while (*text && n < max) {
        int len = 0;
        uint32_t n0;
        while (*text && !lts_letter((char)(*text & ~0x20)))
            text++;
        while (lts_letter((char)(*text & ~0x20))) {
            if (len < LTS_MAXW)
                w[len++] = (char)(*text & ~0x20);
            text++;
        }
        if (!len)
            break;
        n0 = n;
        n = lts_word(w, len, out, n, max);
        if (n > n0)
            out[n - 1] |= MTH_EOW;
    }
    return n;
}
