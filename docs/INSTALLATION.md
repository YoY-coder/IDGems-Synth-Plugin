# IDGems Synth — Installer et tester le plugin (pas à pas)

Le plugin est compilé automatiquement par GitHub à chaque modification, sur une machine Windows
**et** sur une machine Mac. Les deux versions passent les tests de stabilité et la validation
**pluginval** (l'outil qui vérifie qu'un plugin respecte la norme VST3). Tu n'as rien à installer.

---

## Sur Mac (MacBook) — sans installer aucun logiciel

### 1. Télécharger
1. Sur le Mac, ouvre Safari et connecte-toi à GitHub.
2. Va sur le dépôt **IDGems-Synth-Plugin**, onglet **Actions**.
3. Clique sur la dernière ligne avec une **coche verte** (« Compilation VST3 (Windows + macOS) »).
4. En bas de la page, section **Artifacts**, clique sur **IDGems-Synth-VST3-macOS**.
   Safari télécharge et dézippe : dans **Téléchargements**, tu obtiens un dossier qui contient
   `IDGems Synth (macOS).zip`. Double-clique dessus : le fichier **`IDGems Synth.vst3`** apparaît.

### 2. Copier au bon endroit
1. Dans le Finder : menu **Aller → Aller au dossier…** (ou `Cmd + Maj + G`).
2. Tape : `~/Library/Audio/Plug-Ins/VST3` puis Entrée.
   (Si le dossier `VST3` n'existe pas, crée-le : clic droit → Nouveau dossier → `VST3`.)
3. Glisse **`IDGems Synth.vst3`** dans ce dossier. Si une ancienne version existe, ferme Ableton
   d'abord et remplace-la.

### 3. Débloquer le plugin (une seule commande, à refaire à chaque nouvelle version)
macOS bloque les fichiers téléchargés qui ne viennent pas de l'App Store. On retire ce blocage :
1. Ouvre **Terminal** (déjà présent sur le Mac : `Cmd + Espace`, tape « Terminal », Entrée).
2. Copie-colle cette ligne puis Entrée :
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/IDGems\ Synth.vst3
   ```
   Aucun message = c'est bon.

### 4. Dans Ableton Live
1. **Live → Réglages (Préférences) → Plug-ins**.
2. **« Utiliser les dossiers système des plug-ins VST3 »** : **Activé**.
3. Clique sur **Re-scanner**.
4. Dans le navigateur : **Plug-ins → IDGems → IDGems Synth**. Glisse-le sur une **piste MIDI**.

---

## Sur Windows (PC fixe)

1. Sur le dépôt GitHub, onglet **Actions**, dernière ligne avec une coche verte.
2. Section **Artifacts** : **IDGems-Synth-VST3-Windows** → un `.zip` se télécharge.
3. Dézippe-le : tu obtiens le dossier **`IDGems Synth.vst3`**.
4. Copie ce dossier entier dans `C:\Program Files\Common Files\VST3`
   (Windows demande une confirmation administrateur : accepte).
5. Ableton : **Options → Préférences → Plug-ins** → « Utiliser le dossier système VST3 » activé → **Re-scanner**.
6. **Plug-ins → IDGems → IDGems Synth**, sur une piste MIDI.

### Compiler toi-même (facultatif)
Visual Studio : **Fichier → Ouvrir → Dossier…** sur le dossier du dépôt, configuration **x64-Release**,
cible **IDGemsSynth_VST3**, puis **Générer → Tout générer**. Le plugin est créé dans
`out\build\x64-Release\IDGemsSynth_artefacts\Release\VST3\`.

---

## Fichiers
- Réglages de l'interface (taille) : `~/Library/Application Support/IDGems Synth` (Mac),
  `%APPDATA%\IDGems Synth` (Windows).
