# IDGems Synth — synthétiseur VST3 texturé et imprévisible

Version synthé d'[IDGems](https://github.com/YoY-coder/IDGems-Plugin) : même rack d'effets, même système
de random, même DA, avec un moteur de synthèse en dessous. JUCE 8 / C++17 / CMake.

- **Installer / tester** : voir [docs/INSTALLATION.md](docs/INSTALLATION.md)
- **Compilation** : automatique sur GitHub (onglet Actions → artefacts `IDGems-Synth-VST3-Windows`
  et `IDGems-Synth-VST3-macOS`), avec tests de stabilité et validation pluginval.

## Avancement

| Phase | Contenu | État |
|---|---|---|
| 1 | Squelette : instrument VST3, UI redimensionnable, polyphonie, Osc A wavetable, enveloppe de volume, limiteur | ✅ |
| 2 | Moteur de base : Osc A/B complets, Sub, Noise, cross-mod, 2 filtres, poly/mono/legato, glide | |
| 3 | Modulation : enveloppes (déblocage), LFO au crayon, random/S&H, macros, glisser-déposer | |
| 4 | Bus A/B/C + Master, rack IDGems, séquenceur de modules, chaîne de sortie | |
| 5 | Osc Texture : granulaire, spectral, chaos, résonateur, data bending, empreinte spectrale | |
| 6 | Jeu : arpé, séquenceur, gammes, mutation, export MIDI, Note Spray, micro-random | |
| 7 | Random : niveaux, cadenas, seed, historique, morph pad, personnalités, garde-fous | |
| 8 | Création : enregistreur, rolling recorder, Feed, export WAV, batch render, Draft/HQ | |
| 9 | Finitions : DA complète, presets, undo/redo, A/B, MIDI learn, optimisation | |

## Structure du code

```
Source/
  Core/     paramètres, utilitaires DSP (Dsp.h repris d'IDGems)
  Synth/    wavetables à bande limitée, enveloppes, voix et polyphonie
  Engine/   chaîne de sortie (reprise d'IDGems : passe-haut, grave mono, OTT, soft clip, limiteur)
  UI/       thème et LookAndFeel (repris d'IDGems), potards, vues, pages
Tests/      tests de stabilité (NaN, clics, plafond, Panic, état, CPU) + capture d'interface
```

Modules repris d'IDGems (branche de développement, commit `6df54e0`) : `Core/Dsp.h`, `Core/JuceIncludes.h`,
`Engine/OutputChain.h`, `UI/Theme.*`, `UI/LookAndFeel.*`, police Michroma.
