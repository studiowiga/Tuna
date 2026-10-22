# CHROMA — Studio Wiga

Accordeur **VST3** (Windows 64 bits + macOS universel Apple Silicon/Intel) pour guitare et basse, au format pédale
(visuel « CHROMA Polyphonic Tuner », anciennement Tuna). Modes **MONO**, **POLY** (toutes les cordes d'un coup) et **STROBE**,
précis jusqu'aux accordages très graves (G#0, F0, E0 et même D0 ≈ 18 Hz).

## Compiler avec GitHub
1. Mets **tous les fichiers à la racine du dépôt** (avec le dossier `.github/workflows`).
2. Un push lance l'onglet **Actions** → workflow *Build CHROMA VST3 (Windows + macOS)*.
3. À la fin, télécharge les artefacts :
   - `CHROMA-VST3-Windows` → copie `CHROMA.vst3` dans `C:\Program Files\Common Files\VST3\`
   - `CHROMA-VST3-macOS` → copie `CHROMA.vst3` dans `~/Library/Audio/Plug-Ins/VST3/` puis lance une fois
     `xattr -cr ~/Library/Audio/Plug-Ins/VST3/CHROMA.vst3`
4. Dans Reaper : re-scanne les plug-ins. Le plugin apparaît comme **CHROMA (Studio Wiga)**.

Le job *Test du moteur d'accordage* compile `dsp_test.cpp` et vérifie la précision sur des cordes synthétiques
(D0 → E6 en mono, 11 accordages en poly). S'il échoue, le build est signalé en rouge.

## Commandes
| Commande | Rôle |
|---|---|
| **MODE** | MONO → POLY → STROBE (clic = mode suivant, ou tourner). LED au-dessus = mode actif |
| **TUNING** | tourner = accordage suivant / précédent (comme les flèches ◄ ►) · clic = ouvre le menu des accordages |
| **REF** | La de référence 430 → 450 Hz · double-clic = 440 Hz |
| Écran : nom de l'accordage | clic = menu déroulant (ALL / ★ FAVORITES / ✎ CUSTOM), en anglais par défaut ; le petit bouton **FR** de l'en-tête du menu passe les menus de l'écran en français (**EN** pour revenir), choix mémorisé pour toutes les instances. Le nom passe au **vert** quand toutes les cordes sont justes |
| Écran : nom du mode | clic = mode suivant |
| Footswitch **TUNE / MUTE** | coupe la sortie (LED rouge). L'accordeur fonctionne toujours, coupé ou non |

## Clic droit (n'importe où)
- **Plugin size** : défaut (493 px, taille minimale, comme Freeze Shimmer), 60 %, 75 %, 100 % — on peut aussi tirer le coin.
- **MIDI** : canal, *Learn* pour chaque fonction, pédale à verrouillage ou momentanée.
  Par défaut : MUTE = CC 80, MODE = CC 81, accordage suivant = CC 82, précédent = CC 83, REF = CC 84.
  Program Change n = n-ième favori (ou n-ième accordage s'il n'y a pas de favoris).
- **Custom tunings…** / **Show tunings file** : accordages perso et favoris, communs à toutes les instances
  (`%APPDATA%\Studio Wiga\Chroma\library.xml` / `~/Library/Application Support/Studio Wiga/Chroma/library.xml`).
  L'accordage choisi est aussi enregistré dans le projet.

## Fonctionnement (résumé)
- Entrée filtrée puis ramenée à ~11-12 kHz (le grave n'a pas besoin de plus), analysée dans un thread séparé :
  le son passe sans latence et sans coût pour le thread audio.
- **MONO / STROBE** : détection McLeod sur une fenêtre courte et une fenêtre longue (~350 ms pour descendre à 15 Hz),
  test des partiels impairs contre les erreurs d'octave quand la fondamentale est faible, puis mesure fine par
  démodulation à la fréquence de la note et régression de la phase (principe du stroboscope) : ±0,1 cent sur une note tenue.
- **POLY** : une démodulation par corde de l'accordage. Pour chaque corde, le partiel mesuré est le plus bas qui
  ne tombe sur aucun partiel d'une autre corde ; si toutes ses harmoniques sont partagées (cordes à l'octave dans les open tunings),
  la mesure reste juste quand les cordes sont accordées mais moins précise quand elles sont loin : finir en MONO.

Polices IBM Plex Mono / Sans (licence SIL OFL, `OFL-IBM-Plex.txt`).

## Versions
- **1.1.0** — le plugin s'appelle désormais **CHROMA** (nouveau titre sur la pédale). **Supprimez l'ancien `Tuna.vst3`**
  (même identifiant interne, conservé pour que Reaper puisse retrouver le plugin dans les projets existants ; sinon,
  remplacez Tuna par CHROMA dans la chaîne). Les accordages perso et favoris de Tuna sont repris automatiquement. Écran redessiné seulement quand son contenu change, fond mis en cache.
- **1.0.1** — correctif anti-gel de la fenêtre sous Windows (moteur de rendu logiciel).
- **1.0.0** — première version (Tuna).
