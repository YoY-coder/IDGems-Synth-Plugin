# Journal de développement (pour reprendre le travail)

L'utilisateur a demandé d'enchaîner toutes les phases (3 à 9) sans validation intermédiaire, puis de faire
un retour global. À chaque phase : compiler en local, tests de stabilité, commit + push, vérifier la CI.

## Architecture (rappel)
- `Source/Core/Params.*` : tous les paramètres (APVTS). Identifiants stables (automation Ableton).
- `Source/Synth/` : moteur par voix (Osc A/B, Sub, Noise, 2 filtres, enveloppes), `SynthEngine` = polyphonie.
- `Source/PluginProcessor.*` : lecture des paramètres une fois par bloc (`readParams`) -> `VoiceParams`.
- `Source/UI/` : `SynthPage` (disposition Vital), `Graphs` (filtre, enveloppe, unison...), `Widgets` (Knob...).
- Tests : `Tests/StabilityTests.cpp` (exécutable console, lancé par la CI, doit passer avec une pile de 1 Mo).
- Ne jamais mettre de gros objets sur la pile (Windows : 1 Mo).

## Phase 3 — Modulation (plan)
- Sources : 8 macros, 10 LFO (forme au crayon, sync, modes libre / note par voix / note global / one-shot /
  calé hôte, fade-in), 5 random (Perlin, S&H, lisse, Lorenz), ENV 1-8 (par voix), vélocité, note,
  random par note, molette, aftertouch, pitch bend.
- Connexions : 64 slots (source, paramètre cible, profondeur, bipolaire) ; état sauvé par identifiant.
- Application : par voix à cadence de contrôle (16 échantillons) sur une copie locale des `VoiceParams`
  (champs « cibles » listés dans `ModTargets`), paramètres globaux par bloc.
- UI : poignées de glisser-déposer sur chaque source, cibles = potards + graphiques (courbe de filtre ->
  coupure, wavetable -> position), anneaux colorés, Alt/Ctrl + glisser = profondeur, clic droit = liste,
  prévisualisation au survol (cibles d'une source / sources d'une cible), onglet MODULATION = liste des
  connexions. Déblocage ENV 5-8. Éditeur de wavetable au crayon.

## Phases suivantes
- 4 : bus A/B/C + Master avec le rack IDGems (porté), envois par source, bus -> bus sans boucle,
  séquenceur par module, chaîne de sortie complète (OTT, soft clip, limiteur, grave mono, passe-haut...).
- 5 : Osc Texture (granulaire, spectral, chaos, résonateur, noise sculpté, data bending, morph),
  empreinte spectrale, modes granulaire / spectral dans Osc A/B.
- 6 : arpé, séquenceur de notes, générateur de patterns MIDI, gammes, mutation, export MIDI, Note Spray,
  micro-random par voix.
- 7 : système de random (porté d'IDGems) + personnalités + garde-fous.
- 8 : enregistreur, rolling recorder, Feed, export WAV, batch render, modes One-shot/Loop/Texture,
  Draft/HQ, motion recording.
- 9 : DA complète, UI réactive, scope corrompu, undo/redo, A/B, MIDI learn, raccourcis, optimisation CPU.

## État
- Phases 1, 2 : faites. Interface graphique + disposition Vital : faites. Glisser sur la wavetable : fait.
- Phase 3 : faite. `Source/Modulation/` (ModSystem = sources / connexions / cibles, ModEngine = sources globales),
  modulation par voix dans `Voice::computeModulation`, UI dans `Source/UI/ModUI.*` (pastilles, LFO, random,
  liste), potards modulables (Widgets.cpp), crayon wavetable (OscDisplay). Limite : LFO « stéréo » sans effet
  tant qu'il n'y a pas de cibles stéréo (bus FX, phase 4).
- Phase 4 : faite. `Source/Fx/` = rack IDGems porté (registre par rack : préfixes busa_/busb_/busc_/mast_),
  moteur de bus dans `PluginProcessor::processBlock` (tranches de 32 éch.), UI `Source/UI/FxPage.*`.
  Mode sans latence activé par défaut (jeu en direct).
- Phase 5 : faite. `Source/Synth/Texture.h` (GrainCloud, SpectralVoice, TextureVoice, FingerprintFilter),
  analyses dans `SampleData` (SpectralData, FingerprintData, loadRaw), emplacements de sons 2-5 dans le
  processeur (AssetTex1/2, AssetFingerprint, AssetData), onglet `UI/TexturePage.*`. Osc A/B : modes
  Granulaire / Spectral (réglages fins partagés avec la Texture). Boucle Feed : phase 8.
- Phase 6 : faite. `Source/Play/NoteGen.*` (couche MIDI avant le moteur : direct / arpège / séquenceur /
  générateur, Note Spray, gamme ; `generatePattern`), micro-random dans `Voice::start` / `computeModulation`,
  export MIDI `exportMidi`, onglet `UI/PlayPage.*`.
