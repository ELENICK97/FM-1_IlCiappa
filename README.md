# IL CIAPPA

Un firmware alternativo per l'**M-VAVE FM-1** che sa dire una parola sola: **CIAPPA**.
Ogni tasto la canta alla sua nota, e otto modalità la storpiano in modi sempre più strambi.

Costruito su [fm1-base](https://github.com/charlesvestal/fm1-base) e [felucca-bsp](https://github.com/hugelton/felucca-bsp).
Non affiliato con M-VAVE.

## Come funziona

Le voci sono [espeak-ng](https://github.com/espeak-ng/espeak-ng) che dice "ciàppa" in italiano (`tS'ap:a`),
analizzate una volta per tutte da `tools/make_voice.py`: un *pitch mark* per ogni periodo glottale, e i confini di
CI · A · PP · A. Il firmware le legge con un motore **TD-PSOLA** polifonico (8 voci, `app/dsp.c`):

- **l'altezza** dipende da quanto spesso si posano i grani: ogni tasto canta CIAPPA alla sua nota, con i formanti
  della voce intatti (niente effetto criceto, a meno che tu non lo voglia);
- **la durata** dipende da quanto velocemente si avanza nella parola: puoi allungarla o accorciarla senza
  cambiare nota;
- **il timbro** dipende da quanto velocemente si legge ogni grano: dall'ORCO al GNOMO.

Le parti sorde (la "CI", lo scoppio della "PP") restano rumore al loro ritmo naturale, così non diventano un ronzio.

## Comandi

| Comando | Cosa fa |
| --- | --- |
| Tasti | Dicono CIAPPA alla loro nota, più forte quanto più forte li premi. Escono anche come note MIDI su USB. |
| **SELECT** | La modalità (sotto) |
| **PRESET** | La voce: UOMO, DONNA, NONNA, NONNO, A CASO (una diversa a ogni nota) |
| **ALGORITHM** | Il tempo di ARP e della canzone, 60–240 BPM |
| **K1 DURATA** | Quanto dura la parola: ×0.25 … ×4, senza cambiare nota |
| **K2 TIMBRO** | ORCO · OMONE · BASSO · VERA · ALTO · VOCINA · PUFFO · GNOMO |
| **K3 VIBRATO** | 0–100% (fino a un semitono) |
| **K4 ECO** | Un'eco da un terzo di secondo, più scura a ogni ripetizione |
| OCT− · OCT+ | L'ottava, da −2 a +2 |
| **ARP** | Arpeggio: tieni premuti più tasti e li dice a turno, a tempo |
| **PLAY** | "La Ciappa", una canzoncina originale cantata nella modalità e con la voce scelte |
| **HOME** | ZITTO! Ferma tutto |
| **SAVE** | Salva le impostazioni (si accende quando c'è qualcosa da salvare) |
| FX, SEL, ENV, LFO, EDIT, GLO, SEQ, REC | Non servono a niente, quindi dicono CIAPPA a caso (STRACIAPPA, CIAPPISSIMO, CIAPPA 2000…) |
| MIDI in | USB e TRS: suona come i tasti |

## Le modalità

| Sullo schermo | Cosa succede |
| --- | --- |
| **CIAPPA** | Ogni nota canta la parola intera |
| **CIAAAAPPA** | Finché tieni premuto la A si allunga (sullo schermo compaiono le A); quando molli, PPA |
| **NASTRO** | Come un nastro: altezza e velocità insieme. In basso un demone lentissimo, in alto un criceto |
| **APPAIC** | La parola al contrario, ma intonata |
| **CI-CI-CIAPPA** | Balbetta tre volte prima di riuscire a dirla |
| **PAPPACIA** | Finché tieni premuto: sillabe a caso (CIA, PPA, CI, A) a salti d'ottava e di quinta |
| **CORO** | Tre voci diverse in un accordo maggiore, un po' sfasate |
| **ROBOT** | Ogni grano alla nota, anche il rumore; uscita a 4 bit |

Sullo schermo la parola CIAPPA si illumina e saltella lettera per lettera mentre viene detta.

## Compilare e installare

### Senza toolchain: GitHub Actions (consigliato)

1. Crea un repository GitHub e fai push di questa cartella (con il submodule `base`).
2. In **Actions → Build and attest firmware → Run workflow** (oppure con un tag `v0.1`) il builder condiviso
   [`zvenson/fm1-build`](https://github.com/zvenson/fm1-build) compila il firmware, esegue i test e mette
   `ilciappa.fwsc` tra gli artifact (con un tag, in una release).
3. Installa con l'installer web di fm1-base / FoMni (Chrome o Edge, FM-1 via USB) oppure
   `python3 base/tools/fm1_install.py ilciappa.fwsc`.

**Prima di installare** scarica il firmware originale (FM-1 V15, "PC Firmware" su m-vave.com/download): è la
strada per tornare indietro. Firmware sperimentale: lo installi a tuo rischio.

### In locale

    git clone --recursive <questo repository>
    ./build.sh            # app/build/ilciappa.fwsc: serve il toolchain JieLi e l'SDK AC79 (base/README.md)
    sh tests/run_tests.sh # il simulatore: suona ogni modalità, salva WAV e screenshot in app/build/host/

### Rigenerare le voci

Servono espeak-ng e numpy. `app/voice_data.h` è già nel repository.

    tools/make_voice.py /percorso/espeak-ng /cartella/che/contiene/espeak-ng-data app/voice_data.h --wav /tmp/voci

Le voci sono in `VOICES` all'inizio dello script: se ne possono aggiungere (fino a riempire la flash, ~30 KB
l'una) cambiando voce espeak, velocità e altezza.

## File

| File | Cosa |
| --- | --- |
| `app/dsp.c` | Il motore: PSOLA, modalità, arpeggio, canzone, eco |
| `app/app.c` | Tasti, encoder, schermo, impostazioni |
| `app/ciappa.h` | L'interfaccia tra i due |
| `app/voice_data.h` | Le voci, generate |
| `app/tests/basic.txt` | Il test del simulatore |
| `tools/make_voice.py` | Genera le voci da espeak-ng |

## Licenza

MPL-2.0, come fm1-base. Le voci sono generate con espeak-ng (GPL-3.0); il suono che produce non è coperto dalla
sua licenza.
