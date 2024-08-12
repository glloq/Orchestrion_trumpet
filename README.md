> [!NOTE]
> rien n'est fait, je regroupe juste des infos sur une methode pour la trompette

# Orchestrion_trumpet
l'idée de base est d'utiliser un haut parleur de type compression driver afin de créer les notes demandée directement a l'entrée de la trompette.

# les choix techniques 

## les Pistons / valves
Le choix du design actue nous permet d'eviter d'utiliser les pistons lors du jeu mais pour des raisons esthetiques nous utiliserons 3 solenoides pour actionner les valves.

## Compression Driver :
La trompette acoustique a une plage de fréquences approximative de 165 Hz à 987 Hz (de E3 à B5). Cependant, les harmoniques supérieures peuvent monter bien plus haut, jusqu'à environ 10 kHz.
il faudrait choisir un driver avec une reponse en frequence large ou utiliser plusieurs driver afin de couvrir toutes les frequences necessaire.

## Amplificatteur :
Un amplificateur qui peut fournir une puissance suffisante pour piloter votre compression driver avec un bon rapport signal/bruit est nécessaire
Verifier que l'amplificateur puisse gérer la plage de fréquences sélectionnée.

## Module DAC :
Un module DAC (Digital-to-Analog Converter) avec une résolution suffisante (au moins 12 bits) pour reproduire fidèlement les nuances du signal numérique est requis.

# Le signal musical

  ##  Onde de Base 
    
Onde carrée modifiée : Les instruments à vent, comme la trompette, ont un riche contenu harmonique. Une onde carrée modifiée peut être utilisée pour générer ces harmoniques, mais elle doit être adoucie pour éviter un son trop électronique et brut. Pour cela on peut prevoir d' appliquer un filtrage passe-bas pour atténuer les harmoniques supérieures et créer un son plus doux et plus naturel.

  ## Enveloppe ADSR (Attack, Decay, Sustain, Release)
Enveloppe dynamique : La trompette a une attaque rapide suivie d'une décroissance légère, d'un maintien stable, puis d'une libération rapide. Utiliser une enveloppe ADSR pour moduler l'amplitude du signal est essentiel pour capturer la dynamique de la trompette.

    - Attack : Court (pour simuler le punch de la note initiale).
    - Decay : Moyen (pour une transition naturelle vers le sustain).
    - Sustain : Variable, selon la durée pendant laquelle la note est maintenue.
    - Release : Rapide, mais pas instantané.
    
  ## Modulation de frequence
Vibrato : Un léger vibrato (modulation de fréquence) ajouté au signal peut simuler les fluctuations naturelles du souffle du musicien. Une modulation lente (5-7 Hz) avec une faible amplitude (quelques cents autour de la fréquence fondamentale) est généralement appropriée.

  ## Harmonique et Filtrage
Pour imiter une trompette, l'application d'un filtre passe-bas avec un léger boost dans la région des hautes fréquences (autour de 2-5 kHz) peut aider à capturer le brillant naturel de l'instrument. Un filtre passe-haut léger peut également être utilisé pour éliminer les basses fréquences non désirées.

  ## Effets Acoustiques
  Les trompettes bénéficient souvent de l'acoustique d'une pièce. Ajouter une réverbération subtile peut simuler la réflexion du son dans un espace, ce qui ajoute de la profondeur et de la chaleur.

  
