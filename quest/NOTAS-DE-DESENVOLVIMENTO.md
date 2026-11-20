# The Force Engine VR para Quest com motion control: plano

Atualizado em 01/11/2026.

## Ponto de partida

- **APK atual (TheForceEngineVR v0.9.57, So VR Here):** instalado e rodando com o Dark Forces clássico da Steam. O código VR dele não é público (o repositório só tem o motor base), então não serve de base para modificação.
- **Base escolhida:** The Force Engine original (luciusDXL/TheForceEngine, GPL-2), compilado para Android arm64 com camada OpenXR própria, no mesmo modelo do port do Descent (dxx-vr-standalone).
- **Referência de motion control:** QuestZDoom (Team Beef). Licença GPL-3, incompatível com o GPL-2 do TFE para cópia direta de código. Uso apenas como referência de técnica; o código OpenXR vem do nosso port do Descent.

## O que a análise do código mostrou

### QuestZDoom (como ele faz motion control)

- `VrInputDefault.cpp` lê a pose do controle dominante e grava `weaponangles` (yaw, pitch, roll) e `weaponoffset` (posição relativa à cabeça, girada pelo yaw do corpo). Faz o mesmo para a mão secundária (`offhandangles`, `offhandoffset`).
- **Empunhadura com duas mãos:** quando o grip da mão secundária está perto da arma, a direção passa a ser o vetor entre as duas mãos (`weaponStabilised`).
- `gl_openxrdevice.cpp` (fork DrBeef/gzdoom):
  - `GetHandTransform` monta a matriz da arma na mão a partir desses valores.
  - No `SetUp` de cada quadro, o tiro deixa de usar a visão e passa a usar o controle: `AttackAngle`, `AttackPitch`, `AttackRoll` e `AttackPos`, com a flag `OverrideAttackPosDir`.

### The Force Engine (onde encaixar)

- **Disparo:** todas as armas ficam em `TFE_DarkForces/weaponFireFunc.cpp`. Cada função usa `s_playerObject->yaw`, `s_playerObject->pitch` e `posWS` para criar o projétil (`createProjectile` + `proj_setTransform`) e para a mira automática (`computeAutoaim`). São cerca de 10 pontos para trocar por um "override" de mira, o equivalente ao `AttackPos/AttackAngle` do GZDoom.
- **Câmera:** o renderizador GPU (`RClassic_GPU/rclassicGPU.cpp`, `computeCameraTransform`) já usa matriz de câmera com yaw e pitch reais e projeção explícita. Isso permite renderizar em estéreo com perspectiva correta. Falta acrescentar roll, deslocamento por olho e a projeção assimétrica do OpenXR. O renderizador clássico por software (pitch por distorção) fica fora do modo VR.
- **Limite de olhar para cima/baixo:** `player.cpp` limita o pitch (`c_pitchLimits`, máximo 4050 de 16384). No VR, a visão vem da cabeça; o limite deve ser desligado para a câmera e revisto para o projétil.
- **Arma na tela:** `weapon_draw` (`weapon.cpp`) desenha o sprite 2D da arma no buffer da tela. No VR, o mesmo quadro do sprite vira textura de um plano preso ao controle.
- **Backend gráfico:** OpenGL de desktop (shaders `#version 330`/`410`). Precisa de conversão para GLES 3 (`#version 300 es` + precisão); o glshim do port do Descent ajuda.

## Fases

### M0: compilar e rodar em tela plana no Quest

- Toolchain do Descent (ndk-parts, sdk-parts, scripts da VM Linux).
- CMake do TFE para Android arm64 com SDL2 Android, shaders convertidos para GLES 3 e renderizador GPU forçado.
- Dados do jogo em `Android/data/<pacote>/files/` com nomes completos (o adb do PC cortou o último caractere ao copiar para uma pasta; copiar sempre com o nome de destino explícito).
- **Critério:** o menu e a primeira fase rodam no Quest como app 2D.

### M1: estéreo OpenXR

- Reaproveitar `vr_openxr.cpp` do Descent.
- Por olho: câmera = yaw do corpo + pose da cabeça (yaw, pitch e roll), deslocamento do olho e projeção a partir do FOV do OpenXR.
- Menus do TFE (ImGui), PDA, mapa e HUD numa tela virtual à frente.
- **Critério:** a fase é jogável em VR olhando em volta, ainda com controles de gamepad.

### M2: locomoção e controles

- Touch mapeados para as ações do TFE.
- Giro suave ou em degraus (snap).
- Andar na direção da cabeça ou da mão secundária (opção, como no QuestZDoom).
- 6DOF da cabeça (a câmera acompanha a posição da cabeça; o corpo segue sem atravessar paredes).
- Grips como modificadores, no padrão dos outros ports.

### M3: mira pelo controle (núcleo do motion control)

- Estrutura de override da mira: origem = posição do jogador + `weaponoffset` girado pelo yaw do corpo; direção = yaw e pitch do controle.
- Trocar o uso de `s_playerObject->yaw/pitch/posWS` nas funções de `weaponFireFunc.cpp` por esse override quando o VR estiver ativo. Inclui pistola, rifle, repeater, fusion, mortar, mina, concussion, canhão e detonador termal (arremesso com a força do braço, opcional).
- Garantir que a origem do tiro esteja num setor válido (evita atirar através da parede com a mão encostada nela).
- Mira automática desligada por padrão.
- **Critério:** acertar alvos apontando a mão, com o tiro saindo da arma e não da cabeça.

### M4: arma na mão

- Sprite da arma num plano preso ao controle, com escala e posição por arma; alternativa futura: modelos 3D simples.
- Empunhadura com duas mãos para rifle e repeater (estabilização pela mão secundária).
- Opção de mira laser.
- Vibração ao atirar e ao levar dano.

### M5: acabamento

- Ponteiro laser nos menus.
- Opções de conforto.
- Aviso quando faltar DARK.GOB e importação dos dados de dentro do headset, como pedido para o Descent.
- Empacotamento e publicação.

## Riscos

- **Conversão do backend OpenGL para GLES 3:** é o maior desconhecido.
- **Sprites de inimigos e objetos (billboards) com a cabeça inclinada (roll):** podem precisar ficar sempre em pé.
- **Céu e efeitos de tela no modo GPU:** dependem da projeção; precisam de ajuste por olho.
- **Desempenho:** duas renderizações por quadro; multiview fica para depois se for necessário.

## Estado (01/11/2026)

- **Decisão de controles:** mesmo padrão dos outros ports (grips como modificadores, mão direita dominante).
- **Compilação na nuvem do Cowork (sem a VM do PC):**
  - NDK r27c, SDK 34, build-tools 35 (d8) e SDL 2.30.
  - APK montado sem Gradle (`android/build_apk.sh`).
  - O pacote é `com.vcmatias.tfevr`, que convive com o `com.tfe.game` do So VR Here.
- **M0 compilado** (`Downloads\tfe-vr-quest\TFE-VR-Quest.apk`, app 2D):
  - Backend GL convertido para GLES 3.2: contexto ES, glad com fixups, e todos os shaders adaptados na carga para `#version 320 es`.
  - Os shaders validam com o glslangValidator em todas as variantes.
  - Extensões opcionais: `clip_cull_distance` e `noperspective`.
  - Os arquivos de suporte vão no APK e são extraídos para `Android/data/com.vcmatias.tfevr/files`, em modo portable.
- **Instalação:** `instalar_tfe_vr.bat` instala o APK, copia o Dark Forces da Steam com nomes completos, abre o jogo e grava `log_tfe_vr.txt` com o log do TFE e o logcat.
- **Fonte:** o patch sobre o TFE upstream e a pasta `android/` estão em `Downloads\tfe-vr-quest\fonte\tfe-vr-quest-fonte.tgz`, junto com a keystore.
- **Teste no Quest 3 (01/11/2026), feito pela sessão via SideQuest:**
  - M0 roda: GLES 3.2 no Adreno 740, tier 2, `clip_cull_distance` e `noperspective` presentes, shaders compilam e o menu do TFE aparece num painel 2D.
  - Os dados do jogo foram copiados no próprio Quest a partir da pasta `com.tfe.game`.
- **Rotina de teste sem o Victor (a mesma do NOLF):**
  - A sessão usa o SideQuest no PC, liberado com controle total.
  - Aba Sideload: instala o APK pelo seletor, digitando o caminho.
  - Aba Debugging: comandos de shell (copiar arquivos, `monkey` para abrir, `screencap`, logcat).
  - Aba Files: "Download folder" de `/sdcard/tfelog` para `Downloads\tfe-vr-quest`, depois lido pela sessão.
  - O Explorador e os terminais ficam só com clique e não servem para rodar .bat.
- **m0b:** lê `files/tfe_args.txt` como linha de comando (ex.: `-gDARK -c0 -lSECBASE -skip_load_delay`), para entrar direto numa fase. Já está instalado; o teste dentro da fase ficou pendente porque o PC desconectou.
- **m0b no Quest (01/11):** a fase SECBASE carregou (o log mostra o iMuse tocando sons da fase); a captura dessa execução ficou no Quest, em `/sdcard/tfelog`.
- **M1 compilado (02/11/2026), `TFE-VR-Quest-m1.apk`, versão 0.2-m1, ainda não testado:**
  - `android/src/tfe_xr.cpp`:
    - Instância e sessão OpenXR sobre o contexto GLES do SDL, com espaço LOCAL.
    - Swapchains sRGB por olho (gravação crua quando há `EXT_sRGB_write_control`).
    - Uma tela offscreen substitui o back buffer da janela; `RenderTarget::unbind` e as cópias apontam para ela.
    - Essa tela vira uma camada quad de 1,7 m a 1,6 m de distância. Com o mundo visível, o preto da tela fica transparente, para o HUD flutuar sobre o 3D.
  - **Renderizador GPU:**
    - Uma travessia de portais por quadro, com a cabeça e um FOV que cobre os dois olhos.
    - Depois, os passes são desenhados por olho (`drawSceneVR` em `rsectorGPU.cpp`, `vrSetCamera` em `rclassicGPU.cpp`).
    - A projeção do olho inverte y (o TFE usa y para baixo e render targets invertidos), por isso `glFrontFace(GL_CW)`.
    - A câmera é yaw do corpo + pose da cabeça, com 3,3 unidades por metro.
  - **Controles:** os Touch entram como gamepad no `TFE_Input`.
    - Analógico esquerdo anda; analógico direito gira.
    - Gatilho direito atira, gatilho esquerdo faz o tiro secundário.
    - A, B, X e Y seguem o padrão de gamepad do TFE; menu = Guide; cliques dos analógicos trocam de arma.
    - Fora do modo relativo do mouse (menus), o raio do controle direito move o mouse na tela e o gatilho clica.
  - O loader OpenXR da Khronos foi compilado do OpenXR-SDK; o manifesto tem categoria VR e permissões OpenXR.
- **Pontos a conferir no teste do M1:**
  - Sinal vertical da posição da cabeça e o olhar para cima/baixo, que podem vir invertidos.
  - Escala do mundo.
  - Céu.
  - Se a tecla de cor preta não some com partes do HUD.
- **M1 rodando no Quest 3 (02/11/2026, v0.2-m1e):**
  - SECBASE aparece em estéreo, com o HUD flutuando sobre o 3D (a tecla de preto funciona).
  - Captura salva em `tfe-vr-m1-secbase.png`.
- **Correções até a m1e:**
  - Manifesto declara hand tracking (`oculus.software.handtracking` + `HAND_TRACKING`); sem isso o Horizon OS bloqueia a abertura com o diálogo "controles necessários" quando os Touch estão desligados.
  - `gl_ClipDistance` redeclarado com tamanho (`out highp float gl_ClipDistance[8]`), com nova tentativa sem clip distances se o link falhar.
  - `glInvalidateFramebuffer` carregado por `eglGetProcAddress`: o glad para em GL 3.3 e deixava o ponteiro nulo, causando crash no endereço 0.
  - Tela VR fixa em 1280x800, com escala da interface em 100.
- **Para testar sem ninguém no headset:** `setprop debug.oculus.guardian_pause 1` e `am broadcast -a com.oculus.vrpowermanager.prox_close`. Se um diálogo do sistema travar a abertura, remover a tarefa dele (`dumpsys activity activities | grep LaunchCheck`, depois `am stack remove <id>`).
- **Coleta de diagnóstico:** comando na aba Debugging grava em `/sdcard/tfelogN` (screencap, logcat, log do TFE); a aba Files baixa a pasta para `Downloads\tfe-vr-quest`.
- **Próximo passo (M2/M3):** conferir giro e altura com alguém no headset; depois mira pelo controle. Com o Quest ligado no USB, instalar o m1 pelo SideQuest (Sideload), usar `tfe_args.txt` com `-gDARK -c0 -lSECBASE -skip_load_delay` e capturar a tela pela aba Debugging (`screencap`).

## Estado em 02/11/2026, tarde (v0.4-m4c, instalada no Quest)

- **Retorno do Victor sobre a m1e/m3a:**
  - Atravessava paredes.
  - Andar invertido.
  - Recentralizar não funcionava.
  - Menu sem cursor.
  - Pediu mão 3D com textura do jogo e animação dos dedos.
- **Causas encontradas com entrada roteirizada (`files/tfe_xrinput.txt`) e logs de posição (`TFEQuestPos`):**
  - O app abria com o headset na mesa e o espaço LOCAL nascia ali, longe da cabeça e girado.
  - O eixo do TFE estava certo: +1 é para frente. A "inversão" era a cabeça olhando para outro lado em relação ao corpo.
  - O menu do Dark Forces lê `SDL_GetMouseState`; eventos injetados não movem esse estado.
- **Correções:**
  - Referência recentrada (posição e yaw da cabeça), recentralizando sozinha quando a cabeça está a mais de 1 m da referência.
  - Recentralizar pelo botão Meta (evento `REFERENCE_SPACE_CHANGE_PENDING`) e pelo clique do analógico esquerdo.
  - Câmera limitada a 25 cm do corpo.
  - Andar na direção da cabeça.
  - Menu = tecla Esc.
  - Modo menu também quando o mundo não é desenhado.
  - Cursor injetado em `inputMapping.cpp` (`tfe_xr_get_cursor`), com anel amarelo na tela.
- **Mãos 3D (`android/src/tfe_xr_hands.cpp`):**
  - Palma, 4 dedos de 3 segmentos e polegar, com textura RHAND1.BM do jogo.
  - O carregamento espera a paleta sair do fade preto.
  - Grip fecha médio, anelar e mínimo; gatilho dobra o indicador (toque no gatilho = indicador apoiado); toque em A/B/X/Y/thumbrest/analógico dobra o polegar.
  - Desenhadas por olho com o mesmo mapeamento de profundidade do mundo.
- **Roteiro de teste (`files/tfe_xrinput.txt`):** linhas "início duração nome valor [valor2]". Nomes: `move`, `turn`, `trigR`, `trigL`, `a`, `b`, `x`, `y`, `menu`, `stickL`, `stickR`, `aim` (yaw pitch em graus), `hands`, `grip`, `thumb`. Sem o arquivo, nada muda.
- **Pendências:**
  - Esconder o sprite 2D da arma e pôr a arma na mão.
  - Iluminar as mãos pelo setor.
  - Mapa de controles definitivo (grips como modificadores).
  - Giro em degraus.

## Estado em 02/11/2026, noite (v0.5, instalada no Quest)

- **Pedido do Victor:** deixar o jogo com cara de "feito para VR", decidindo sozinho, em ciclos de pesquisa, teste e refação.
- **O que a pesquisa (Flat2VR, Team Beef) aponta como marca de jogo feito para VR:** arma física na mão e mira pelo controle, HUD diegético (pulso e arma), vibração, giro em degraus, agachar de verdade, soco com o braço, troca de arma por gesto, empunhadura com duas mãos e opções de conforto.
- **Implementado na v0.5:**
  - Arma 2D escondida (`weapon_draw` só informa o estado em VR); mira (retícula) desligada.
  - Modelos 3D low-poly das 10 armas (caixas e cilindros) na mão direita, com a mão fechada no cabo e o indicador no gatilho; detonador e mina na palma; punhos fechados.
  - O tiro sai da boca da arma (`hands_muzzle` alimenta `tfe_xr_get_aim`).
  - Clarão no cano, mira laser (opção) e luz das mãos e arma pela luz do setor, lanterna e clarão do tiro.
  - HUD de vida e escudos no pulso esquerdo e munição sobre a arma (camadas quad próprias, recortadas da tela; `hud.cpp` informa os retângulos, y medido de cima).
  - Tela de mensagens e menus segue a cabeça devagar durante o jogo.
  - Giro em degraus de 45° (opção suave), troca de arma com o analógico direito para cima e para baixo, corrida automática com velocidade pelo analógico.
  - Agachar de verdade (cabeça 35 cm abaixo da altura recentrada), com compensação da altura do olho do jogo.
  - Soco: com os punhos, um jab rápido (mais de 1,8 m/s) dispara.
  - Gesto de ombro: mão direita atrás da cabeça + grip troca de arma.
  - Duas mãos: grip esquerdo perto da frente da arma estabiliza a mira.
  - Vibração no tiro (força por arma) e ao levar dano.
  - Vinheta de conforto ao andar.
- **Mapa de controles:** A usa/abre, B pula, X agacha, Y mapa; clique do analógico direito = lanterna; grip esquerdo é o modificador (grip+A visão noturna, grip+X máscara de gás, grip+Y PDA); clique do analógico esquerdo recentraliza; menu = Esc.
- **Opções em `files/tfe_vr.ini`:** `snap_turn`, `snap_angle`, `smooth_turn_speed`, `laser_sight`, `wrist_hud`, `physical_crouch`, `crouch_depth`, `haptics`, `screen_follows_head`, `comfort_vignette`.
- **Teste sem headset:**
  - Roteiro ganhou `cheat N` (6 = LAPOSTAL), `wpn N` (tecla da arma), `wrist` e `gripL`.
  - `hosttest/` (no pacote de fonte) compila o módulo das mãos no PC da nuvem com GL falso e renderiza as malhas em Python (vistas de olho, lado e topo), para ajustar a geometria sem passar pelo Quest. Foi assim que se achou a curvatura dos dedos invertida.
- **Verificado no Quest por screencap:** pistola e rifle na mão com laser e munição na arma, clarão, HUD no pulso esquerdo, giro em degraus, troca de armas, menu Esc com cursor.
- **Falta validar com os controles de verdade:** soco, gesto de ombro, duas mãos, agachar, vibração e se a posição da mão bate com a mão real.

## Estado em 02/11/2026, noite (v0.6, voxel)

- **Pedido do Victor:** mãos e armas em pixel art 3D (voxel).
- **Mãos:** cada segmento (palma, falanges, polegar) virou um bloco de cubos de 5,8 mm com as arestas longas cortadas. Luva preta com as cores tiradas do RHAND1.BM, punho branco e manga laranja do macacão.
- **Armas:** extrusão em voxel dos sprites de pickup do próprio jogo (vistas laterais), lidos do DARK.GOB do usuário em tempo de execução, sem embutir dados do jogo no APK:
  - IST-GUNI (rifle), IAUTOGUN (repeater), IMORTAR, IFUSION, ICONCUS, ICANNON, IDET (esfera), IMINE (disco deitado).
  - A espessura vem da distância até a borda do sprite, o que arredonda o volume.
  - A tabela `c_voxSpec` traz, por arma, o pixel do cabo, o pixel da boca, o tamanho do voxel e a espessura máxima.
  - A pistola Bryar não tem sprite de pickup, então foi desenhada à mão em pixel art no mesmo estilo.
- **Desempenho:** a malha é montada uma vez por quadro em coordenadas do app e serve aos dois olhos (um único VBO com `glBufferSubData`).
- **Ferramentas:** `voxel/dfdata.py` lê GOB, FME, BM e PAL em Python; o hosttest carrega os sprites (`sprN.rgba`) para ver os modelos sem o Quest.
- **Verificado no Quest:** pistola, rifle, repeater e detonador em voxel na mão, com laser e HUD no pulso.

## Estado em 02/11/2026, noite (v0.8d)

- **Pedidos do Victor:**
  1. Sem efeitos sonoros.
  2. Relógio na mão esquerda no lugar do HUD.
  3. Teclado virtual para digitar.
  4. Mapa no relógio.
  5. Botão de correr (andar devagar por padrão).
  6. Armas mais detalhadas, pesquisadas.
  7. Tiro saindo da ponta da arma.
  8. Grips como modificadores, com o tiro alternativo no modificador.
  9. Armas maiores.
  10. Bomba na posição certa da mão.
  11. Relógio na horizontal.
  12. Mapa original (2D) na tela do relógio, sem holograma.
- **Sem SFX: causa e correção.** O Android marca os ponteiros do heap no byte mais alto (0xB4...). O iMuse usa o bit 63 do ImSoundId para indicar MIDI, então todo efeito sonoro virava MIDI inválido e falhava com "null sound addr". A correção foi `android:allowNativeHeapPointerTagging="false"` no manifesto; os erros de som zeraram.
- **Relógio:**
  - Computador de pulso em voxel no antebraço esquerdo (pulseira e caixa metálica na horizontal, com LED).
  - A tela mostra vida/escudo (HUD do jogo recortado, fundo opaco).
  - Com Y, a mesma tela mostra o automapa original e a tela grande some.
  - Analógico direito para cima/baixo dá zoom; com o grip direito, troca de camada.
- **Teclado virtual:**
  - Aparece sob a tela quando um campo de texto do DF está ativo (`updateEditBox` chama `tfe_xr_request_keyboard`).
  - Mira com o raio, gatilho digita.
  - Tem teclas aA, ←, ENTER e SPACE; envia SDL_TEXTINPUT e eventos de tecla.
- **Correr:**
  - O clique do analógico esquerdo ativa a corrida até você parar de empurrar o analógico (`run_mode=0` no tfe_vr.ini faz segurar).
  - Sem isso, anda devagar.
  - O autorun forçado em VR foi removido.
- **Recentralizar:** grip esquerdo + clique do analógico esquerdo, ou o botão Meta.
- **Controles (R = grip direito, L = grip esquerdo):**

  | Entrada | Ação | Com modificador |
  |---|---|---|
  | Gatilho direito | tiro | R: tiro alternativo |
  | A | usar | R: visão noturna; L: quicksave (Alt+F5) |
  | B | pular | R: botas de gelo; L: quickload (Alt+F9) |
  | X | agachar | L: máscara de gás |
  | Y | mapa no relógio | L: PDA |
  | Clique do analógico direito | lanterna | |
  | Gatilho esquerdo | arma anterior | L: guardar a arma |
  | Analógico direito | giro de 45° nos lados; troca de arma para cima/baixo | |

  - Gestos mantidos: soco, pegar no ombro, duas mãos.
- **Armas detalhadas (`android/src/tfe_xr_weapons.cpp`):**
  - Sólidos em mm (caixas, cilindros, esferas e entalhes) voxelizados em grade de 3 a 4,5 mm, com textura pixel art por voxel.
  - A malha é estática por arma (VBO próprio, matriz de modelo).
  - O cabo mantém o tamanho real e o resto escala: Bryar 1,25, E-11 1,35, repeater 1,6, fusion 1,45, morteiro 1,4, concussion 1,35, canhão 1,45.
  - Referências:
    - E-11 = Sterling L2A3: jaqueta perfurada oca com o cano dentro, power pack lateral, contador Hengstler, T-tracks, mira M38 e coronha dobrada.
    - Bryar = linha DL-44/C96, com bobina dourada do sprite do DF.
    - Morteiro Packered: corpo cromado, 3 cartuchos, shroud perfurado e coronha fina com placa.
  - Detonador e mina centrados no punho.
- **Tiro saindo do cano:**
  - Em VR os offsets de projétil do DF viram ~0 (z=0.3, sem pitch/yaw de convergência).
  - A origem é a boca da arma (`hands_muzzle`), corrigida pelo mesmo clamp de cabeça da câmera (`headCorrection`).
  - O teste antigo usava uma origem fixa do modo roteirizado (bug corrigido).
  - A recentralização automática passou para 0,6 m.
  - Verificado com `slowmo 0.002`: o raio nasce na ponta do E-11.
- **Ferramentas de teste novas no roteiro:**
  - `slowmo v`: escala do tempo do jogo.
  - `headrel`: mãos seguem a cabeça atual.
  - `wrist`: pose de olhar o relógio, braço cruzado.
  - `kbd`, `run`, `recenter`, `gripL`, `gripR`.
  - Log `VRFire`/`VRCam` no log do TFE.

## v0.9 (02/11/2026, noite)

- **Pedido do Victor:** usar também, como base do voxel, o modelo mais detalhado que o jogo usa, isto é, as armas em primeira pessoa (BM `pistol1`, `rifle1`, `therm1`, `autogun1`, `fusion1`, `mortar1`, `clay1`, `concuss1`, `assault1`, que são renders de modelos 3D).
- **Ajustes feitos a partir desses sprites:**
  - Paleta dos metais em azul-acinzentado escuro, como nos renders.
  - Repeater: corpo quadrado com duas fileiras de pinos de latão, carcaça frontal arredondada e topo frisado.
  - Fusion cutter: 4 canos grossos lado a lado, com listras diagonais laranja de perigo (padrão helicoidal `cylZStriped`) e pontas pretas arredondadas.
  - Morteiro: lóbulos cromados e carcaça inferior escura.
  - Mina: etiqueta de perigo vermelha e branca e luz azul.
  - Canhão: faixa marrom-ferrugem.
- **Roteiro de teste de uso único:** a partir da v0.9.1, o `tfe_xrinput.txt` é apagado assim que é lido.

## v0.9.5 (02/11/2026, noite)

Verificado no Quest:

- **Fusion cutter:** listrado, aparece na mão.
- **Mão e arma por cima do cenário:** o buffer de profundidade é limpo antes de desenhá-las, para o cano não entrar na parede.
- **Morteiro, concussion e canhão:** aparecem na mão.
- **Mapa no relógio:** a escala é 2,5 vezes maior em VR, com mínimo de 6.
- **Detonador e mina:** a munição deles saiu de cima da arma, porque cobria o objeto na palma, e foi para o pulso direito.
- **Mina:** refeita como o render do jogo (clay1): disco prateado deitado com raios laranja, etiqueta vermelha e branca, e luzes.
- **Teclas das armas:** confirmadas (7 = mina, 8 = morteiro, 9 = concussion, 0 = canhão).

## v0.10a (02/11/2026, noite)

- **Pedidos do Victor:**
  1. Começar do zero, com todas as cutscenes.
  2. O botão de configurações dentro do jogo não funcionava; criar um tópico de configurações de VR.
  3. O mapa não ficava contido no relógio; opção de holograma projetado, estilo Star Wars.
- **Início do jogo:**
  - `tfe_args.txt` agora é `-gDARK -c1` (com cutscenes). O `-c0` desligava as cutscenes e o `-lSECBASE` pulava direto para a fase.
  - Testes usam `files/tfe_args_test.txt`, de uso único e apagado ao abrir. Se o `tfe_args.txt` não existir, é criado com `-gDARK -c1`.
- **Configurações:**
  - O botão Configuração do menu Esc abre o menu de sistema do TFE (ImGui) por cima do jogo. Antes ele não entrava em modo menu (sem cursor) e o preto ficava transparente.
  - Agora `menuMode` inclui `TFE_FrontEndUI::isConfigMenuOpen()`/console, a tela fica opaca e parada, e as máscaras do HUD são desligadas.
- **Aba VR no menu de configurações** (`configVR` em `frontEndUi.cpp`, em português):
  - Giro em degraus ou suave, com ângulo e velocidade.
  - Vinheta.
  - Modo de correr.
  - Agachar de verdade e profundidade.
  - Ajuste de altura (sentado).
  - Botão de recentralizar.
  - HUD no relógio.
  - Mapa no relógio ou em holograma.
  - Mira laser.
  - Tela segue a cabeça e distância da tela.
  - Vibração e intensidade.
  - As opções ficam em `tfe_vr.ini` (API C `TfeVrOptions`/`tfe_xr_options`/`tfe_xr_save_options`).
- **Mapa:**
  - Recorte central (64%) da tela em volta do jogador, máscaras do HUD e dilatação 5x5 no shader para engrossar as linhas de 1 px.
  - Swapchain do relógio passou para 768x480 e o zoom de VR para 1,6x.
  - Holograma (opção, ou R+Y para alternar):
    - Camada quad de 30 cm acima do relógio, virada para a cabeça, com swapchain 1024x640.
    - Shader azul com scanlines e painel translúcido.
    - Cone de luz aditivo saindo do relógio até os cantos do holograma.
- **Teste:** iniciado, mas o PC desconectou da sessão no meio. O guardian_pause ficou em 1 no Quest (volta ao normal ao reiniciar o Quest).

## v0.10i (03/11/2026, madrugada)

- **Menu de configurações:** o ImGui desenhava fora da textura da tela virtual, porque usava o tamanho da janela Android. Agora, em VR, `TFE_Ui::begin` força DisplaySize = 1280x800. O mouse do ImGui só aceita eventos do raio do controle (`which = TFE_XR_MOUSE_ID`); antes, eventos do ponteiro da superfície Android deslocavam o X em cerca de 800 px. A aba VR abre e os cliques funcionam (testado no Quest).
- **Início do zero:** confirmado no Quest: abre com as cutscenes (logo Star Wars e Dark Forces).
- **Mapa:**
  - Bug ao andar com o mapa aberto: a tela 2D não era limpa em VR, e as linhas do automapa se acumulavam em manchas verdes que continuavam visíveis depois de fechar o mapa (pareciam o "mapa tradicional"). Agora `beginRender` limpa a cor do alvo virtual em VR (`jediRenderer.cpp`), e o mundo nunca é desenhado na tela 2D em VR (`rsectorGPU.cpp`).
  - O mapa nunca aparece na tela grande da frente: só no relógio ou no holograma.
  - O botão Y alterna: fechado, primeira visão (opção da aba VR), a outra visão, fechado. O R+Y foi removido.
  - Holograma menor (18 cm), 14 cm acima do relógio no eixo vertical do mundo, com cone de luz mais visível, raios nas bordas, ponto emissor e moldura com bordas suaves.
- **Disparo:** testado com câmera lenta. O tiro sai da ponta da arma e segue a linha do laser até o impacto.
- **Ferramenta de teste:** o comando `dump N` do script grava `tfe_dumpN.ppm` com a tela 2D (jogo + UI).

## v0.11c (03/11/2026, madrugada)

- **Configurações na interface original:** o botão CONFIGURATION do menu Esc abre, em VR, a tela "config" original do MENU.LFD, com a fonte font8 e acentos desenhados por cima (`GameUI/vrMenu.cpp`, desenho em `android/src/tfe_vrmenu.cpp`). O jogo fica pausado enquanto ela está aberta. O botão de baixo à esquerda troca de página:
  - JOGO: volumes, legendas, aviso de segredo, segredos no PDA e "Opções do motor" (abre o menu do TFE).
  - VR: as 12 opções de VR.
  - CONTROLES: ajuda com desenho dos dois Touch em pixel art e a lista de comandos.
  - Testado no Quest: abre, os cliques funcionam e o slider muda com o clique.
- **Mapa:** são 3 modos exclusivos (relógio, holograma ou original); o Y só abre e fecha. As linhas do automapa ficam fora da faixa de baixo da tela, para não aparecerem no HUD da arma. O holograma usa as cores originais do mapa com brilho.
- **Traços pretos no chão:** os planos de corte dos portais ganharam folga de 0.35 unidade (`VR_CLIP_SLACK` no cabeçalho ES dos shaders), porque cada olho fica alguns cm fora da cabeça. Ainda não confirmado no lugar dos prints.
- **Tiro:** mira automática desligada em VR; primeiro projétil sem deslocamento lateral; dispersão aleatória reduzida a 1/3. No log, a posição inicial do disparo está exatamente na linha do cano.
- **Polegar:** fecha em punho com garra + gatilho.
- **Luva:** voxels 3x menores (1,9 mm), seções arredondadas, couro preto com brilho, antebraço sem manga (pele) como na arte original. Malhas estáticas por segmento (VBO + matriz).
- **Armas:** sprites de primeira pessoa (PISTOL1, RIFLE1, THERM1, AUTOGUN1, FUSION1, MORTAR1, CLAY1, CONCUSS1, ASSAULT3) projetados no modelo voxel. Pose da arma por arma ajustada offline pela silhueta (`hosttest/fit`). Na construção, a silhueta é aparada, as partes que faltam são extrudadas e as cores vêm da arte. Voxels 30% menores.

## v0.12b (03/11/2026)

- **Armas:** três estilos na aba VR (ARMAS): ALTA RESOLUÇÃO (malha lisa iluminada, padrão), VOXEL (os modelos antigos, de volta como estavam) e VOXEL 3X (os mesmos modelos com voxels 3x menores em cada eixo). A projeção dos sprites foi retirada. As malhas lisas e o voxel 3x são gerados numa thread de fundo na primeira vez que a arma aparece (`weapons_buildSmooth`, `weapons_buildModel(..., detail 3)`, surface nets em `tfe_xr_mesh.cpp`).
- **Mãos:** luvas táticas pretas estilo Counter-Strike, protetor de nós dos dedos, palma de camurça, punho com velcro, manga bege e relógio no pulso esquerdo. Malha lisa por SDF, gerada em thread de fundo (antes travava a thread principal por segundos); enquanto não fica pronta, usa a mão voxel. Confirmado no Quest (tfelog55).
- **Troca de arma estilo Half-Life: Alyx:** analógico direito segurado para baixo abre uma roda na mão direita com as armas que o jogador tem; mover a mão escolhe; soltar seleciona. Toque rápido para baixo = arma anterior; para cima = próxima.
- **Arma na parede:** a ponta da arma é testada contra as paredes do setor (`tfe_df_vr_wall_penetration` em player.cpp); se atravessaria, a mão e a arma recuam ao longo da mira.
- **Soco:** com os punhos, cada soco faz a mão avançar 17 cm e voltar (0,28 s).
- **Pendente de teste no Quest:** roda de armas, recuo na parede, animação de soco, VOXEL 3X, traços pretos no chão.
- **Próximo:** modelagem mais detalhada e fiel às referências Star Wars para o estilo ALTA RESOLUÇÃO (hoje são as formas antigas suavizadas).

## v0.13f (03/11/2026)

- **Armas ALTA RESOLUÇÃO novas** (`tfe_xr_hiweapons.cpp`): modelos por campos de distância (sólidos arredondados, bordas suavizadas, furos, ranhuras, parafusos, lentes), malha fina (0,55 mm, até ~400 mil triângulos), materiais procedurais (aço escovado, pintura gasta, madeira, borracha, listras de perigo, luzes) e oclusão de ambiente gravada nos vértices. Geradas em segundo plano; a arma da mão primeiro e depois o resto do arsenal. Confirmado no Quest (E-11 e Bryar).
- **Mãos:** malha de mão com esqueleto (WebXR Input Profiles "generic hand", licença MIT, Amazon), subdividida 2x (`tools/hand/subdiv.py`), animada na GPU por 25 juntas; luva preta com palma de camurça. Polegar com oposição na base e flexão nas duas articulações. Sem braços: a direita termina no punho da luva; a esquerda no fim do relógio, que ficou mais perto da mão.
- **Arma na parede:** a mão nunca é movida. Quando o cano entraria na parede, mão e arma são desenhadas escaladas em direção à cabeça (mesma imagem nos olhos, menos profundidade), ficando à frente da parede. Confirmado no Quest.
- **Menus na nave:** menu inicial, cutscenes e menu de pausa aparecem no console de uma sala de nave imperial, com janela para o espaço. Paredes, chão, teto, faixas, console e bancos de computador usam as texturas do próprio jogo (TEXTURES.GOB + SECBASE.PAL, lidos pelo motor), carregadas dos arquivos do jogador. Opção "MENUS NA NAVE" na aba VR. Confirmado no Quest.
- **Mapa:** sempre holograma (opções relógio/original removidas). As linhas do mapa no renderizador GPU ignoravam o recorte; agora são cortadas antes do HUD.
- **Traços pretos no chão:** causa encontrada: as paredes eram cortadas pela oclusão vista do centro da cabeça; cada olho vê um pouco mais e sobrava uma fresta preta ao longo das silhuetas, prolongada pelo chão e teto. Agora cada pedaço de parede é estendido 3 unidades ao longo da própria parede. Em VR também: cor verdadeira com mipmaps e bilinear nítido.
- **Ajuda:** desenho dos dois Touch com linhas ligando cada botão à sua função (sem lista).

## v0.14e (03/11/2026)

- **Traços pretos (correção definitiva):** a extensão das paredes da 0.13f criava faixas pretas largas e foi removida. Agora cada olho percorre os portais da sua própria posição (`vrSetCamera(eye, true)` + `traverseScene` por olho em `drawSceneVR`), então os cortes de oclusão coincidem com o que cada olho vê. Folga dos planos de corte reduzida para 0,02.
- **Roleta de armas:** a lista de armas do jogador era lida uma vez só, no início da fase (código fora do laço da tarefa do jogador); agora é atualizada a cada quadro. Abre segurando o clique do analógico direito; clique rápido = lanterna; analógico para cima/baixo = próxima/anterior. Discos mais visíveis, raio 15 cm, a arma da mão some enquanto a roleta está aberta, ícones com os modelos voxel leves. Confirmado no Quest (todas as 10 armas).
- **Polegar:** fechado só com o polegar encostado no controle; levantado = joinha (também segurando arma).
- **Pausa com PDA:** em jogo, o menu de pausa aparece num PDA na mão esquerda, virado para os olhos; o mundo continua visível e congelado; HUDs do relógio e da arma somem. A sala da nave fica para o menu inicial e cutscenes. A tela de configuração VR (que usa o renderizador software) ainda abre com a sala ao fundo. Confirmado no Quest.
- **Ajuda:** analógico direito atualizado (cima/baixo, clique, segurar).
- **Modelos de armas da internet:** pendente. Os modelos de Star Wars encontrados (Sketchfab, CGTrader, MakerWorld, Thangs, Printables, Thingiverse) exigem login para baixar ou bloquearam o download automático; os de impressão 3D vêm em peças separadas e sem cor.

## v0.15a (03/11/2026, não instalada: Quest desconectado do SideQuest)

- **Armas do Dark Forces Voxel Pack** (Dzierzan, https://github.com/Dzierzan/Dark-Forces-Voxel-Pack): IPISTOL, IST-GUNI, WDET, IAUTOGUN, IFUSION, IMORTAR, IMINE, ICONCUS, ICANNON convertidos em `android/src/tfe_xr_fanvox.h` por `tools/fanvox/gen.py`. Licença do pacote: uso não comercial, com atribuição, só com cópia legítima do Dark Forces (`tools/fanvox/LICENSE-Dark-Forces-Voxel-Pack.txt`). Cada arma toma o comprimento e a boca do cano do modelo anterior (tiros e laser continuam alinhados). Padrão novo da opção ARMAS: VOXEL (pacote); outras: ALTA RESOLUÇÃO e VOXEL ANTIGO (3x). Chave do ini mudou para `weapon_set`.
- **Mão direita** some quando segura uma arma (só a arma aparece); punhos continuam com a mão.
- **PDA preso à mão esquerda:** segue a mão; a mão o segura (dedos retos por baixo, polegar sobre a borda), tela a 2,6 cm da palma.
- **Grip esquerdo + Y:** 1º toque abre o mapa (holograma), 2º fecha o mapa e abre o PDA do jogo (objetivos, briefing...), 3º fecha. Y sozinho ficou livre. O PDA do jogo aparece no PDA da mão; como ele usa o renderizador software, a sala da nave aparece ao fundo.

## v0.15b (03/11/2026, instalada e testada no Quest)

- 0.15a confirmada no Quest: E-11 do Voxel Pack na mão (mão direita oculta), grip+Y abre o mapa (holograma), grip+Y de novo abre o PDA do jogo no PDA da mão esquerda (sala ao fundo).
- **Arma na parede, novo padrão "engolida":** mãos e arma são desenhadas com a profundidade do mundo (sem limpar o depth), então a parte da arma dentro da parede some atrás dela. O painel de munição da arma some quando o cano entra na parede (a munição vai para o pulso direito). Modo antigo ("sempre na frente", com a arma escalada para perto da cabeça) continua na aba VR do menu do motor ("Arma na parede"); chave `weapon_wall` (0 engolida, 1 na frente).

## v0.16b (03/11/2026, instalada; teste visual no headset pendente)

- **Voxel Pack removido** (arquivos e código). As formas do pacote são as corretas para a lore; as armas próprias foram remodeladas nos dois estilos (VOXEL e ALTA RESOLUÇÃO):
  - Bryar: pistola longa e fina, dois anéis dourados no cano, corpo na parte de trás, haste sob o cano, cabo de madeira inclinado.
  - Repeater: carcaça frontal grande (janelas amarelas, tambor frisado) sobre estrutura de haste fina, cabo de madeira e placa de coronha.
  - Fusion cutter: compacto, 4 canos empilhados na vertical com faixas amarelas.
  - Morteiro: esguio e longo, tubo frontal pesado, tubo traseiro fino, coronha de madeira inclinada.
  - Concussion: cano prateado em cima, câmara vermelha abaixo do meio, coronha de madeira.
  - Canhão: cano longo e fino, corpo traseiro grande em rampa.
  - E-11, detonador e mina mantidos. Opção ARMAS: ALTA RESOLUÇÃO / VOXEL / VOXEL FINO.
- **PDA segurado pela lateral esquerda:** tela de frente para o jogador (normal no sentido do punho do controle), dedos envolvendo a traseira, polegar logo fora do canto inferior esquerdo da tela. Pose ajustada offline (`hosttest/pdafit.cpp`): nenhum vértice da mão dentro do aparelho, pontas dos dedos encostadas na traseira, erro do polegar ~1 mm.
- **PDA do jogo sem sala da nave:** o mundo continua ao fundo, congelado. Cópia em meia resolução de cada olho (antes das mãos), redesenhada com reprojeção de rotação e escurecida enquanto o PDA está aberto.
- **Bolinha do cursor removida:** na tela fica só o cursor do jogo (o teclado virtual mantém o marcador).
- A 0.16b fechava ao entrar na fase: `glTexStorage2D` é nulo no glad 3.3 (pc 0 em `tfe_xr_draw_hands`). As capturas com a passagem de câmera eram o Quest depois do crash.

## v0.16c (03/11/2026, instalada e testada no Quest)

- Textura da cópia do mundo criada com `glTexImage2D`. Sem crash.
- Confirmado no Quest: PDA do jogo na mão esquerda, segurado pela lateral esquerda com o polegar no canto inferior esquerdo, com o corredor da SECBASE congelado ao fundo (sem a sala da nave). Repeater e fusion cutter novos aparecem na mão.

## v0.17a (03/11/2026, não instalada: Quest desconectado do PC)

- **Armas remodeladas na 0.16 descartadas.** As antigas voltaram como estavam nas opções ALTA RESOLUÇÃO, VOXEL e VOXEL FINO.
- **Novo estilo REMASTER (padrão), no estilo do Dark Forces Mod Remastered para Jedi Academy** (modelado do zero, nada do mod é usado; referências: imagens do mod no ModDB e os renders de primeira pessoa do DF):
  - Materiais: preto laqueado brilhante com bordas gastas até o metal, gunmetal polido, latão e dourado vivos, luzes acesas. O shader liso ganhou reflexo de ambiente (luzes de corredor imperial) para esses materiais.
  - Bryar: cano grosso preto envolto por bobina de latão densa, bico alargado, receptor angular, trilho sob o cano.
  - Repeater: corpo preto arredondado e robusto, sulcos laterais com pinos de latão e fileira de luzes amarelas, bico com dois canos.
  - E-11: luneta grande (1,45x).
  - Demais armas: formas antigas com os materiais do remaster. O tiro sai da boca do modelo REMASTER.
  - Chave do ini: `weapon_look` (0 alta resolução, 1 voxel, 2 voxel fino, 3 remaster).
- **Cheats de teste no menu (CONFIGURAÇÃO, página JOGO):** "TESTE: ARMAS" dá todas as armas com munição cheia (LAPOSTAL); "TESTE: INVENCÍVEL" liga/desliga a invencibilidade (LAIMLAME). Valem ao fechar o menu.

## v0.18b (03/11/2026, instalada e testada no Quest)

- **REMASTER descartado.** O estilo 3 agora é **DFMOD**: as armas de primeira pessoa do Dark Forces Mod Remastered para Jedi Academy (DFmod 2025 Final, ModDB), extraídas da cópia baixada no PC do Victor (Downloads\DFmod_2025_Final.zip).
  - `tools/dfmod/`: leitores MD3 e GLM, `assemble.py` (monta cada arma como o JKA: modelo posicionado pelo `tag_weapon` do *_hand.md3, cano pelo `tag_barrel`, sem o antebraço) e `convert.py` (pega da arma, boca do cano e escala por arma; texturas até 512 px com o mapa _spec do mod no alfa) gerando `dfmod_weapons.bin`.
  - Os arquivos do mod não entram no fonte: o `.bin` vai só no APK (`DFMOD_BIN=... android/build_apk.sh`) e é extraído para `files/`.
  - Armas: Bryar (briar_pistol), E-11 (blaster_r), repeater (heavy_repeater), fusion (demp2), morteiro (golan_arms), concussion (c_rifle), canhão (merr_sonn). Detonador e mina continuam com os modelos de alta resolução.
  - Desenho próprio: textura + luz + brilho do mapa _spec, dois lados (os modelos de primeira pessoa não têm as faces de trás). O tiro sai da boca do modelo DFmod.
  - Sem o `.bin`, DFMOD usa as armas de alta resolução.
- Confirmado no Quest (72 fps): Bryar com a bobina, E-11 com a luneta, repeater e morteiro na mão.
- Os modelos são os de primeira pessoa do JKA: vistos de trás ou de baixo, algumas partes não existem (o jogo original só mostrava um lado).

## v0.18c (03/11/2026, instalada e testada no Quest)

- **Armas DFmod completadas.** Os modelos de primeira pessoa do JKA só têm o lado que aparecia na tela; o conversor agora completa cada um (`tools/dfmod/convert.py` + `hull.py`):
  - **Lado espelhado:** nas armas simétricas (Bryar, repeater, fusion, morteiro, concussion, canhão) o lado que faltava é o espelho do lado modelado, em torno do eixo do cano, só onde o modelo não tinha superfície (não duplica o que já existe). A E-11 fica sem espelho (luneta e power pack só de um lado).
  - **Casco fechado:** um volume pelas silhuetas lateral, de cima e de frente (casco visual), recuado ~3 mm, colorido pela textura mais próxima. Ele aparece onde o modelo original tem buracos; onde há superfície texturizada, ela fica na frente.
  - Formato `dfmod_weapons.bin` versão 2 (vértice com cor; submalha -2 = casco).
- Confirmado no Quest: Bryar vista pelo lado esquerdo (bobina inteira) e repeater fechado com as luzes amarelas nos dois lados.

## Decisão de ferramenta (03/11/2026)

- **Modelagem 3D é feita no Blender da nuvem** (Blender 4.2 LTS sem janela, em /opt/blender, comandado por scripts Python): armas, mãos, PDA e objetos. Os scripts ficam em `tools/dfmod/` (e equivalentes) e geram os dados que o jogo carrega.
- Motivo: até a 0.18c as armas e mãos foram feitas com ferramentas próprias em código (campos de distância, voxel, casco por silhuetas), reimplementando de forma limitada o que o Blender já faz bem (edição de malha, tampar buracos, booleanas, UV, bake de textura). O casco da 0.18c ficou ruim por isso.
- As ferramentas próprias ficam só para o que roda dentro do jogo.
- Para ajustes à mão, entregar os .blend em Downloads\tfe-vr-quest para o Victor abrir no Blender do PC; o que ele ajustar é convertido de volta.

## v0.19a (03/11/2026, instalada; teste no headset interrompido)

- Armas DFmod fechadas no Blender (`tools/dfmod/prep.py` → `blender_caps.py` → `pack.py`): faces soldadas, aberturas tampadas (holes_fill + triangulação beauty) com a cor da textura vizinha; geometria e texturas originais intactas; lado espelhado nas armas simétricas.

## v0.19b (04/11/2026, gerada; não instalada: controle do PC interrompido pelo Victor)

- **Pesquisa (lore):** Imperial repeater: corpo angular, carcaça triangular com 3 canos, tambor de munição de um lado e a fita visível do outro, cabo de madeira atrás, coronha fina saindo da base do cabo. Packered mortar: cilíndrico, cromado, cano com abertura grande e capa perfurada, 3 cartuchos que giram, coronha fina terminando em placa plana, ganchos de bandoleira atrás da capa. Jeron fusion cutter: ferramenta de mineração de mão, 4 câmaras em linha horizontal. K-16/Bryar: pistola preta convencional, parte central maior com aletas de refrigeração. Fontes: starwars.archivum.wiki (imperial_repeater_rifle, Packered_mortar_gun, Jeron_fusion_cutter, K-16_Bryar_Pistol), strategywiki (Dark Forces/Weapons).
- **Blender (`tools/dfmod/blender_finish.py`):** peças que o modelo de primeira pessoa nunca teve, modeladas com chanfro, UV e textura assada de materiais procedurais (cor × oclusão de ambiente):
  - Repeater: tambor de munição no lado que faltava (com tampa e pinos de latão), coronha de duas hastes finas saindo da base do cabo, placa de coronha com borracha. O repeater deixou de ser espelhado (a fita só de um lado, como na descrição).
  - Morteiro: coronha fina com placa plana e borracha, dois ganchos de bandoleira.
  - Fusion cutter: cabo embaixo do corpo (com borracha) e gatilho; escala menor (0,65).
  - Espelho seletivo: só peças perto do eixo do cano (Bryar: a alavanca lateral fica só de um lado; E-11: só o cano).
  - Canhão: textura do Merr-Sonn (azul no mod) convertida para cinza escuro metálico, como o canhão do dark trooper.
- **.blend de cada arma** (modelo do mod + tampas + peças novas, texturas embutidas) em Downloads\tfe-vr-quest\blend, para ajuste à mão.
- Pipeline: `prep.py` → `blender_caps.py` → `blender_finish.py` → `pack.py` (→ `dfmod_weapons.bin`); `blender_preview.py` gera imagens de conferência (modo `final`) e os .blend.

## v0.20 (04/11/2026, gerada; não instalada: Quest desligado pelo Victor, testes no headset pausados)
- **Arsenal inteiro remodelado no Blender** (tools/weapons): bryar, e11, thermal, repeater, fusion, mortar, mine, concussion, cannon. Cada arma é um script que usa kit.py (primitivas com bevel, booleanos, materiais PBR) e termina em `finish()`: junta, suaviza por ângulo, UV, bake de cor, brilho e AO (Cycles CPU), exporta `<nome>.npz` (vértices no referencial da arma) e `<nome>.png` (RGB + brilho no alfa).
- **Detonador térmico e mina** agora também têm modelo próprio (antes caíam no estilo alta resolução).
- **Cromado:** recebe um reflexo de estúdio falso assado na cor (chão escuro, faixa clara no horizonte), porque o shader do jogo não tem mapa de ambiente; cromado puro parecia plástico branco.
- **Luzes emissivas:** faces de material glow* exportam vCol.r = 2; o shader do estilo 3 mistura a cor da textura sem sombreamento nessas faces (acendem no escuro).
- **Desgaste de borda:** superfícies grandes com recortes usam `blackplain` (desgaste baixo) para evitar manchas cinzas.
- **Empacotamento:** `python3 tools/weapons/pack.py /home/claude/wout out.bin 1024` gera o DFMW v2 (uma submalha texturizada por arma, textura reduzida a 1024). O build_apk.sh copia via `DFMOD_BIN=` para assets/dfmod_weapons.bin.
- **Opção:** o estilo 3 passou a se chamar BLENDER (menu ARMAS e front-end). ALTA RESOLUÇÃO, VOXEL e VOXEL FINO continuam.
- **Fila de bake:** /home/claude/wout/queue.sh roda scripts e revisões em sequência via setsid (sobrevive ao limite de 10 min do shell); rv.sh gera a folha de revisão de 5 vistas.
- Entregas: TFE-VR-Quest-0.20.apk, fonte/tfe-vr-quest-fonte-0.20.tgz, blend/armas_blender_0.20.tgz (.blend + texturas), tfe-vr-0.20-armas.png.
- **Pendente no headset:** conferir escala, posição na mão, orientação da textura e o brilho das luzes no Quest.

## v0.21b (04/11/2026, instalada; teste visual no headset interrompido: Quest desconectado)
- **Mão direita segurando as armas** (opção "MÃO DIREITA NA ARMA", página JOGO do menu de configuração; "Mão direita na arma" no menu do motor; chave `right_hand`, padrão ligada).
- **Apenas o indicador se move:** o gatilho do controle leva o indicador do repouso no gatilho até puxado; dedos, polegar e posição da mão ficam fixos. O botão de grip e o toque do polegar deixam de mexer na mão armada.
- **A mão acompanha o estilo da arma:** VOXEL e VOXEL FINO usam a mão voxelizada (voxel de 5,8 mm e 1,9 mm, como as armas); ALTA RESOLUÇÃO e BLENDER usam a mão lisa com esqueleto. O PDA continua com a mão lisa.
- **Empunhadura padrão** (`tools/weapons/kit.py`: `std_grip`, `std_trigger`): todas as armas de cabo (Bryar, E-11, repeater, fusion, morteiro, concussion, canhão) usam o mesmo cabo, a mesma guarda aberta e o mesmo gatilho, 2 cm abaixo dos corpos para o pulso passar por baixo deles. O canhão teve o corpo traseiro e a célula de energia elevados; fusion, E-11 e concussion ganharam a caixa do gatilho mais alta; o cabo do fusion subiu para o padrão.
- **Pose ajustada offline** (`tools/grip/gripfit.cpp` + `sdf.py`): campo de distância de cada arma no Blender; a mão (malha com esqueleto) é ajustada sem atravessar a arma, com contato da palma e dos dedos, o polegar do lado esquerdo e a ponta do indicador no gatilho. Resultado (`tools/grip/std_pose.txt`): nenhuma penetração no cabo padrão; nas armas reais, no máximo 2 mm. Detonador (segurado por cima, dedos sobre a esfera) e mina (pela borda) têm poses próprias.
- Novos controles na skin: `handskin_setIndexOverride` (articulações do indicador) e `handskin_setFingerOverride` (médio, anelar, mínimo).
- Roteiro de teste ganhou `handpos x y` (posição da mão direita falsa).
- Pendente no headset: conferir a mão em cada arma e o indicador puxando o gatilho.

## v0.22 (05/11/2026, gerada sem teste no headset, a pedido do Victor)
- **Munição na parede da arma:** o painel saiu de cima/dentro da arma e fica colado na parede esquerda do corpo, levemente inclinado para cima. O ponto é achado na malha desenhada (qualquer estilo): entre 11 cm à frente e 5 cm atrás do cabo, a coluna com a parede esquerda plana mais alta. Visto pelo outro lado, o painel some (antes ele aparecia através da arma). Painel menor (5 cm).
- **PDA com a moldura da tela de configurações:** a caixa do PDA usa a arte do próprio jogo (fundo do config.anim do MENU.LFD, paleta do menu), com o painel interno escurecido sob a tela e os textos dos botões de baixo apagados. A tela do PDA fica sobre o painel interno da moldura. Sem os arquivos do jogo, volta a caixa lisa.
- **Tamanho do PDA:** opção nova (página "MÃOS E PDA" do menu de configuração, que também recebeu "MÃO DIREITA NA ARMA"; menu do motor: "Tamanho do PDA"; chave `pda_size`, 0,6 a 1,6). O PDA cresce a partir do canto que a mão esquerda segura.
- **Mão no PDA refeita** (`tools/grip/pdafit2.cpp`): borda esquerda presa entre o polegar e os dedos; o polegar descansa na moldura esquerda (fora da tela), apontando para cima e para dentro, sem cruzar; dedos na traseira, sem atravessar a caixa.
- **Mapa piscando ao abrir o PDA:** a tela grande fica oculta enquanto o mapa fecha e o PDA abre (até o PDA aparecer), e por alguns quadros sempre que o mapa fecha (a tela ainda tinha as linhas do mapa).
- **Cursor nas configurações:** a tela de configuração VR desenha o cursor do próprio jogo (cursor.delt).
- **Repeater:** a parte de trás arredondada não segurava a caixa do gatilho nem o topo do cabo (ficavam soltos embaixo); ganhou um bloco traseiro inferior que liga tudo ao corpo. As laterais da traseira arredondada foram recuadas 2 mm (sumiu a mancha escura).
- **Gatilho anima com o dedo:** no estilo BLENDER a lâmina do gatilho (marcada na cor do vértice pelo pack.py) gira em torno do topo, até 0,40 rad, acompanhando o indicador. Nos estilos voxel e alta resolução o gatilho continua fixo.
- **Polegar da mão voxel:** sai da base da palma do lado do indicador; aberto, sobe ao lado do indicador (joinha); fechado, passa por fora dos dedos dobrados, oposto a eles. Posição calculada para a ponta cair na lateral do indicador dobrado (conferido em renderizações do teste local `hosttest/voxhand.cpp`).
- Pendente no headset: tudo acima.

## v0.23 (05/11/2026, gerada sem teste no headset)
- Peças soltas corrigidas: na Bryar, a placa e a argola de cordão agora encostam na base da empunhadura; no canhão, a célula de energia subiu 5 mm e ganhou um berço preso ao corpo (conferido em render Blender).
- HUD de munição no fundo (face traseira) da fusão e do canhão no estilo BLENDER, inclinado para o olho e oculto quando visto por trás; as demais armas seguem com o HUD colado na parede esquerda.
- Tablet no formato do menu de missão: caixa com a arte de pda.anim (quadro 0 do MENU.LFD), 20 cm de largura × escala do tablet, janela interna 23..304 × 11..166.
- Ao abrir o menu de missão, a imagem ocupa a face inteira do tablet com a moldura do próprio jogo, sem moldura dupla. Configurações e pausa continuam dentro da janela.
- Piscada ao abrir o tablet: o primeiro quadro do menu fica oculto (s_screenHide = 3) até o tablet assumir a tela.
- Pegada do tablet refeita para o novo corpo (pdafit3, custo 2,56, sem penetração).
- Gatilho animado: triângulos da lâmina marcados no pack (cor g = 2) e girados junto com o indicador.
- versionCode 69.

## v0.24 (05/11/2026, gerada sem teste no headset)
- Nave: as mesas de botões (console central e consoles laterais) agora sobem em direção à tela, com o teclado voltado para o jogador; o console central ganhou um apoio sob a borda traseira e os laterais, uma base nova.
- Nave piscando ao abrir o tablet: nos 10 quadros seguintes ao último quadro do mundo (e enquanto o tablet é pedido), a imagem congelada do jogo fica no fundo no lugar da sala da nave.
- Piscada preta nas escadas: cada olho agora percorre os portais a partir do setor onde ele realmente está (vrEyeSector em rsectorGPU.cpp: setor do corpo, vizinhos até dois portais, depois sector_which3D). Antes, com a cabeça já no degrau seguinte, a travessia partia do setor do corpo e descartava tudo à frente.
- Luva voxel removida: a luva padrão aparece em todos os estilos de arma.
- Pegada do tablet refeita como "pegada de livro" (a pegada usual de tablet com uma mão): borda esquerda apoiada na palma e na membrana do polegar, dedos abertos e estendidos nas costas do tablet, polegar deitado no bezel esquerdo fora da tela. Novo ajuste hosttest/pdafit5.cpp com rotação livre do tablet na mão (yaw 0,058, tilt -0,285, roll -0,557) e articulações por dedo (handskin_setFingerJoints).
- Mão direita apontando para o tablet (cursor ativo): a arma some, o indicador fica estendido e dobra levemente no clique; a arma volta 25 quadros depois de sair do tablet.
- versionCode 70.

## v0.25 (06/11/2026, gerada sem teste no headset)
- Teclado da nave: textura trocada de IPDETPNL (placa de circuito) para IWPANEL3 (fileiras de botões do Dark Forces), um ladrilho na profundidade da mesa, linha de cima na borda do fundo.
- Pegadas refeitas com referências (granada: mão envolvendo o corpo, polegar na alavanca; frisbee "power grip": polegar em cima, quatro dedos sob a borda), novo ajuste hosttest/palmfit.cpp com articulações por dedo e rotação livre:
  - detonador térmico: esfera na palma, dedos envolvendo, polegar na placa de armar;
  - mina: polegar deitado na face de cima, dedos sob a borda, borda traseira na membrana do polegar.
- Tablets:
  - borda arredondada como no relógio (raio 11 mm, chanfro de 1,5 mm nas duas faces);
  - menus e configurações: moldura da tela de configurações (config.anim), janela 53..268 × 13..167;
  - menu de missão: moldura do PDA (pda.anim) com o conteúdo do jogo recortado na janela 23..304 × 11..166 (pda.cpp informa onde a tela 320x200 cai no framebuffer); os botões da moldura continuam clicáveis e ganham um ponto de mira na moldura.
- Peças soltas (verificação automática tools/weapons/floatcheck.py em todas as armas, com PARTS_BLEND):
  - E-11 (arma 2), lado esquerdo: alavanca de engatilhar e botão da guarda encostados na arma;
  - repetidora: placa do fundo da janela de munição no fundo do rebaixo, luzes de carga que estavam enterradas foram para a lateral direita, tambor dentro da silhueta do corpo;
  - fusão: luzes de status (enterradas) no topo da culatra;
  - canhão: aletas presas ao cone, lente vermelha no fundo da tampa da mira;
  - concussão: parafusos no fundo do rebaixo, mira encostada.
- Roda de armas: o lado esquerdo (morteiro, mina, concussão, canhão) escolhia a arma errada, porque o ângulo da mão saía negativo; agora o ângulo vai de 0 a 2π como os espaços da roda.
- versionCode 71.

## v0.26 (06/11/2026, gerada sem teste no headset)
- Tablet com moldura ao vivo: a cada quadro, a tela 320x200 do jogo (moldura do PDA ou das configurações, com botões e cursor) é copiada para uma textura (glBlitFramebuffer da tela, invertida na vertical) que reveste a face arredondada do tablet; a camada de tela mostra só a janela interna. Assim os botões da moldura acendem ao clicar, e as configurações ficam com uma moldura só (a do próprio jogo).
  - pda.cpp ganhou `pda_vrReportCanvas()` (posição da tela 320x200 no framebuffer), chamada pelo PDA e, em mission.cpp, para os menus de pausa.
  - Janelas: missão 23..304 × 11..166; configurações 53..268 × 13..167. As artes estáticas (pda.anim e config.anim) ficam só como reserva antes da primeira cópia.
  - O ponto de mira extra na moldura saiu: o cursor do jogo aparece na moldura.
- Pulso removido de novo: a malha da mão termina na base da palma (o toco do pulso foi comprimido num arremate arredondado em `handskin`), e o punho da luva saiu da mão direita. A esquerda mantém o punho curto que leva o relógio do HUD.
- versionCode 72.

## v0.27 (06/11/2026, gerada sem teste no headset)
- Máscara de gás vista por dentro (quando o jogador usa a máscara do jogo, `s_wearingGasmask`, informada por player.cpp com `tfe_xr_set_gasmask`):
  - Modelo no Blender (`tools/gasmask/gasmask.py` → `gasmask.bin` em assets_extra e `gasmask.blend`), inspirado nas máscaras panorâmicas de visor único (família M50/M53): vedação de borracha e anel do visor, rebites nas têmporas, insinuação do bocal/filtro embaixo, no centro.
  - Sutil: cada metade do visor é desenhada em volta do olho daquele lado (sem a paralaxe empurrar a moldura para dentro da vista), abertura de 53° para a têmpora, 44° para cima e 48° para baixo; a moldura só aparece na borda do campo de visão.
  - Lente: tinta fria muito leve, escurecendo perto da moldura, um reflexo fino na diagonal e o embaçado da respiração subindo de baixo a cada expiração (4,2 s; 3 s correndo).
  - Presa à cabeça, desenhada por último em cada olho (`tfe_xr_mask.cpp`), acompanha a luz do ambiente e a lanterna, aparece e some em 0,4 s. Aparece no jogo e no mundo pausado do tablet, e fica fora da sala da nave.
  - Opção "MÁSCARA DE GÁS VISÍVEL" na página "MÃOS, PDA E MÁSCARA" do menu VR (também no menu do motor), chave `gas_mask_view`.
- versionCode 73.

## v0.28 (06/11/2026, gerada sem teste no headset)
- Empunhadura da mão direita refeita com base em referências de tiro (membrana entre polegar e indicador alta na traseira do cabo, médio, anelar e mínimo envolvendo a frente do cabo sem espaço vazio, polegar reto ao longo da arma, indicador no gatilho):
  - novo ajuste `hosttest/gripfit3.cpp`: articulações por dedo (nó de cada dedo, articulações do meio e da ponta comuns), dedos com dobra parecida entre si (o mínimo envolve o cabo como os outros), mínimo obrigado a ficar acima do fim do cabo, polegar ao longo da lateral, e a mão pode girar (rx) para seguir a inclinação do cabo;
  - resultado: o mínimo deixou de ficar encolhido na palma e os três dedos descem pelo cabo; vale para todas as armas de cabo padrão (Bryar, E-11, repetidora, fusão, morteiro, concussão, canhão).
- versionCode 74.

## v0.29 (06/11/2026, gerada sem teste no headset)
- Máscara de gás: abertura um pouco menor (51° têmpora, 41° cima, 45° baixo) para a moldura aparecer de fato na borda; log "gas mask on/off". O cheat de teste do menu VR virou "ARMAS E ITENS" (LAMAXOUT: armas, munição e todos os itens, inclusive a máscara, que o LAPOSTAL não dá). A máscara se coloca com garra esquerda + X.
- Velocidade: andar agora é metade do antigo andar; L3 = trote (o antigo andar); L3 + garra direita = corrida (a antiga corrida). Garra esquerda + L3 continua recentralizando. Com "correr: liga até parar", o nível fica travado até soltar o analógico.
- Opção "ANDAR NA DIREÇÃO": da cabeça (padrão), do controle esquerdo ou do controle direito (`walk_direction` 0/1/2), no menu VR e no menu do motor.
- Opção "MOSTRAR FPS" no menu VR (grava `show_fps` das configurações gráficas do TFE).
- Imprecisão das armas: o Dark Forces original sorteia um desvio a cada tiro (`variation` de cada arma, sem mira automática no VR); a versão VR cortava esse desvio a um terço. Agora o padrão é o desvio original, com opção "IMPRECISÃO DAS ARMAS: ORIGINAL/REDUZIDA" (`weapon_spread`, vale a partir da missão seguinte).
- Tablet: páginas sem moldura própria (tela de controles e menu Esc) voltam a usar a moldura de configurações com a página encaixada na janela; menus com moldura e PDA de missão seguem com a moldura ao vivo.
- Mira de duas mãos (garra esquerda perto da frente da arma) removida.
- Usar/abrir portas: o raio de uso do jogo agora sai na direção da cabeça (corpo do jogo + giro real do jogador), então funciona também quando o jogador gira o corpo de verdade.
- versionCode 75.

## v0.30 (06/11/2026, gerada sem teste no headset)
- Máscara de gás 3D: a exportação do Blender lia as cores de vértice como lineares (0,004, praticamente preto), então a máscara era desenhada invisível; agora usa `color_srgb` e cores de borracha um pouco mais claras. A máscara 2D original do jogo (sobreposição plana) deixou de ser desenhada no VR.
- Corrida: L3 + qualquer garra (esquerda ou direita) corre; L3 sozinho trota. A recentralização por garra esquerda + L3 saiu (o botão Meta faz isso). Tela de controles atualizada.
- FPS desligado por padrão (settings padrão `show_fps = false` e, uma única vez, desligado nas configurações existentes; religa no menu VR).
- Tela de controles: sem moldura própria; aparece dentro da mesma moldura de configurações das outras páginas, recortada pela tela 320x200 do jogo.
- Configurações saindo do tablet: o tablet agora mantém a última posição se o controle esquerdo perder o rastreio por um instante, e tolera 10 quadros sem o aviso do menu (antes 3).
- Tiro na ponta da arma (E-11 e demais armas longas): o deslocamento até a boca do cano era limitado a 2,2 unidades a partir do corpo; o E-11 com o braço estendido passava disso e o tiro saía atrás do cano. Agora o tiro sai da boca desenhada (limite 4 unidades), só recua se a boca estiver dentro de uma parede, e nasce 14 cm à frente da boca.
- Mão esquerda: um único toco curto da luva, começando dentro da base da mão (sem vão) e terminando logo depois do relógio, que ficou 6 mm mais perto da mão; a segunda peça do punho e a pulseira saíram.
- versionCode 76.

## v0.31 (06/11/2026, gerada sem teste no headset)
- Sujeira na lente da máscara de gás (procedural, no passe da lente em `tfe_xr_mask.cpp`): três digitais (sulcos curvos), duas manchas de gordura, dois riscos finos, poeira esparsa (mais perto da moldura) e gotas secas com borda clara. Aparecem mais com luz (lanterna, salas claras) e somem junto com a máscara.
- versionCode 77.

## v0.41 (17/11/2026, gerada sem teste no headset)

- **Novo estilo de arma DFMOD** (opção ARMAS, quinto estilo; "DFmod" no menu do motor; `weapon_look` = 4). BLENDER continua como padrão.
  - Origem: Dark Forces Mod Remastered para Jedi Academy (DFmod 2025 Final, da cópia do Victor). O modelo de primeira pessoa (`*.md3`, detalhado, só o lado que aparecia na tela) é completado pelo modelo de mundo do próprio mod (`*_w.glm`, usado por NPCs e no chão, fechado dos dois lados).
  - `tools/dfmod/register.py`: encaixe do modelo de mundo sobre o de primeira pessoa (rotação, escala e translação por ICP aparado a partir das 24 rotações de eixo). Morteiro com restrição: a boca (w_exit) perto da boca do modelo de primeira pessoa.
  - `tools/dfmod/combine.py`: refino por peça (ICP rígido, aceito só se melhora o encaixe e move pouco; corrigiu o cabo duplo da Bryar). Os triângulos de primeira pessoa ficam todos; os do modelo de mundo entram onde não há superfície de primeira pessoa a menos de 0,3 unidade (subdivisão adaptativa nas bordas).
  - `tools/dfmod/pack_comb.py` → `dfmod_combined.bin` (DFMW v2, texturas até 1024 px com o _spec no alfa). Pega: origem 2,4 cm acima e 1,8 cm à frente do centro do cabo do próprio modelo (Bryar, E-11, morteiro, concussion); repeater, fusion e canhão usam a pega antiga (convert.SPEC). Fusion ganha o cabo modelado na 0.19 (w5_add). Detonador e mina: modelos Blender. Canhão recolorido para cinza escuro. Morteiro: tiro sai da boca do modelo de mundo.
  - Código: dois conjuntos de armas texturizadas em `tfe_xr_hands.cpp` (`s_dfmSet[2]`, arquivo escolhido pelo estilo), cópia das texturas na CPU liberada após o envio à GPU. APK: `DFMOD2_BIN=/home/claude/dfmod/dfmod_combined.bin` no build_apk.sh.
  - Os arquivos do mod não entram no fonte; o `.bin` vai só no APK.
- Pendente no headset: posição da mão em cada arma (pose da mão é a das armas Blender), escala e o alinhamento dos tiros.

## v0.40 (13/11/2026, gerada sem teste no headset)
- Tiro: o projétil agora sempre nasce atrás, na linha do cano, no ponto mais próximo do corpo (projeção do olho sobre a linha de mira, até 2,5 unidades antes do cano), então acerta qualquer coisa entre o jogador e o cano, inclusive parede. Ele fica invisível até passar do cano: createProjectile avisa weaponFireFunc (weapon_vrProjectileCreated) dentro do bloco de tiro VR, e o renderizador GPU pula o objeto enquanto a distância percorrida for menor que a distância até o cano (tfe_df_vr_projectile_hidden; registro expira em 60 ticks). Substitui a correção de queima-roupa da 0.39.
- Voxels nas paredes: interruptores (ZASWIT*), painéis de computador (ZACOMP1) e o olho (ZAEYE) usados como textura de sinal viram peças 3D presas à parede. vox_prepareLevel lê os nomes das texturas da fase (slot base em s_levelState.textures[i + count]), aceita quando o voxel tem a largura da textura e o número de quadros bate com a animação (ou é quadro único); cada quadro da textura (estado do interruptor, animação do painel) escolhe o voxel _A, _B... A posição repete a conta do shader do sinal (base no piso, no teto do vizinho ou no piso do vizinho conforme a parte; deslocamento u = offset da parte - signOffset.x; v = signOffset.z; densidade de texel = length / comprimento geométrico). Lado da sala: normal (uz, -ux), o lado em que u corre para a direita de quem olha; eixo x do modelo = -u para manter a orientação dos objetos 3D. A profundidade é o eixo mais fino do .vox (y: frente = y baixo; z: frente = z alto, cima = +y). Ficaram de fora as tiras empilhadas (SWSWITCH, IWFUELS, ZW4WAY, IASWBLUE), cujo estado depende do deslocamento da textura, e texturas de parede inteira (ZASPIN).
- versionCode 86.

## v0.39 (13/11/2026, gerada sem teste no headset)
- Descanso do polegar (thumbrest) virou botão configurável: slots DESCANSO ESQ. e DESCANSO DIR. (ação OpenXR "thumbrest", /input/thumbrest/touch de cada mão, com filtro de alguns quadros para não piscar). Padrão: esquerdo = MAPA, que no descanso funciona como "segurar": o mapa abre enquanto o polegar está lá e fecha ao tirar (só se foi o descanso que abriu). Direito: NADA. Outras ações no descanso ficam ativas enquanto o polegar estiver nele. Chaves bind_rest_l / bind_rest_r no tfe_vr.ini; tela de controles com as duas linhas novas.
- Tiro à queima-roupa: o tiro sai do cano, que pode estar além de um inimigo colado no jogador, e aí o projétil nascia depois dele e não acertava. Agora, antes de cada disparo, o VrAimScope procura atores (ETFLAG_AI_ACTOR) do setor do jogador e dos vizinhos cujo cilindro cruza o caminho olho -> cano + passo inicial; se houver, o projétil nasce 0,3 antes do primeiro (log "VRFire point blank").
- Voxels (Dark Forces Voxel Pack, de Dzierzan, licença não comercial com atribuição, cópia em Voxels/LICENSE.txt e README.txt): 157 modelos .vox (itens, armas no chão, decoração, droides, projéteis) vão no APK em Voxels/ com index.txt. tfe_vox.cpp lê o MagicaVoxel (as cores já são índices da paleta do Dark Forces: cor c = índice c-1), faz malha gulosa (faces visíveis da mesma cor juntadas) e cria JediModel com quads planos e normais unitárias (flag MFLAG_VOXEL); escala = altura do quadro do sprite / altura em pixels, base no pé do sprite (offsetY), centro no objeto, frente do voxel para o yaw do objeto. No renderizador GPU, todo sprite com voxel é desenhado como modelo 3D (iluminado pelo setor; fullbright mantido). Escolha por estado: quadros _A, _B... seguem a animação 0; _DAMAGED no estado destruído (anim 1); droides: _01.._12 andando, _ATTACK/_MELEE_ATTACK/_RANGE_ATTACK_1/2 nos ataques, _PAIN na dor; morte e explosões continuam sprite. vox_prepareLevel roda antes do upload dos modelos para a GPU; modelos em cache por arquivo e escala. Opção "OBJETOS EM VOXEL" na página 2 das opções VR (chave voxels). Ficaram de fora as texturas de parede/interruptores, decalques e o MOUSEBOT (que já é 3D).
- versionCode 85.

## v0.38 (07/11/2026, gerada sem teste no headset)
- Tradução das imagens do jogo (fase 2), com o idioma "pt":
  - Briefings das 14 missões (DFBRIEF.LFD, DELT de cada fase): mesmas figuras e barras, texto em português desenhado com as fontes do próprio briefing (glifos aprendidos das imagens originais por OCR de pixel exato, acentos desenhados no mesmo estilo). Os parágrafos são refeitos nas linhas originais, inclusive ao lado das figuras; linhas a mais empurram o resto para baixo. Títulos das barras traduzidos (nomes em itálico, Executor e Arc Hammer, ficam como no original).
  - Objetivos do PDA (ANIM de cada fase: quadro 0 lista, quadros 1.. objetivo cumprido em amarelo) refeitos com as fontes sombreadas do jogo, espaçamento de linha 11 para caber acentos.
  - Menu Esc (ABORTAR, AVANÇAR, CONFIGURAR, SAIR DO JOGO, CONTINUAR) e confirmações (OBJETIVOS CONCLUÍDOS/INCOMPLETOS, TEM CERTEZA?, NÃO/SIM) em MENU.LFD.
  - Menu do agente (MENU DO AGENTE, AGENTES, MISSÕES, NOVO AGENTE, REMOVER AGENTE, SAIR, INICIAR MISSÃO) e diálogos (REMOVER AGENTE, DIGITE SEU NOME:) em AGENTMNU.LFD.
- Motor: TFE_Paths ganhou setArchivePatchProvider (paths.cpp e paths-posix.cpp). Ao adicionar um arquivo LFD à busca, o provedor do Dark Forces (darkForcesMain.cpp) coloca na frente dele Translation/pt/<NOME>.LFD, que só tem os recursos traduzidos; ao remover, o par sai junto. Vale para DFBRIEF.LFD, MENU.LFD e AGENTMNU.LFD.
- Ferramentas em tools/translate: lfdlib.py (LFD/DELT/ANIM/PLTT), brief_seg/brief_ocr/brief_slots/brief_render.py (briefings), glyphfont.py, goals_lib/goals_learn/goals_render.py (objetivos), relabel.py e menus_pt.py (botões e diálogos). Textos em tools/translate/brief/pt/*.txt e brief/goals_pt.txt. Os LFD do jogo ficam fora do pacote de fonte (vêm da pasta LFD da Steam).
- Ainda em inglês: botões pequenos do PDA (MAP, WEAP, EXIT; INV, OBJ e MIS já servem), "SECRETS:" dos objetivos, letreiro de abertura (FTEXTCRA.LFD) e menus do TFE (ImGui).
- versionCode 84.

## v0.37 (07/11/2026, gerada sem teste no headset)
- Cursor nos menus do TFE (ImGui): o ImGui não desenha cursor, então a camada da tela desenha um ponto (anel amarelo, centro branco, raio 9 px) onde o raio do controle toca a tela, quando o modo relativo está desligado (front-end do TFE) ou o menu de sistema/console está aberto. As telas do Dark Forces continuam com o cursor do próprio jogo.
- Tradução dos briefings: depende do DFBRIEF.LFD (fica na pasta LFD do jogo da Steam, fora do DARK.GOB). O pedido de acesso à pasta da Steam foi negado; aguardando o Victor copiar a pasta LFD para Downloads\tfe-vr-quest\lfd.
- versionCode 83.

## v0.36 (07/11/2026, gerada sem teste no headset)
- Abrir o tablet com o mapa ligado: o jogo fecha o mapa ao abrir o tablet em VR (mission.cpp, s_drawAutomap = 0 e tfe_xr_set_automap(0)); o holograma do mapa não aparece com o tablet aberto (mapOn exige !s_pdaMenu). O botão do tablet com o mapa aberto só manda o F1.
- Mexer no tablet (mapa de missão) girava o personagem: o movimento do mouse (cursor no tablet) é zerado para o jogador enquanto o tablet está aberto (player.cpp, handlePlayerMoveControls).
- versionCode 82.

## v0.35 (07/11/2026, gerada sem teste no headset)
- Mapa e zoom separados: o botão MAPA (padrão GE+Y) só abre e fecha o mapa. Nova ação "ZOOM DO MAPA" (XRA_MAPZOOM, padrão GD+Y): com o mapa aberto, 1º e 2º apertos afastam (x0,6 cada), o 3º volta à vista próxima de abertura (tfe_df_vr_map_zoom_reset, escala 4). Migração única (zoom_bind_done) coloca no GD+Y quando estava livre.
- versionCode 81.

## v0.34 (07/11/2026, gerada sem teste no headset)
- Tablet de missões corrigido (na 0.33 ele ficava vazio): com o renderizador GPU ligado (necessário para o mundo continuar ao vivo nos olhos), a tela 2D da CPU nunca chega à GPU. Agora o pda_update compõe o tablet numa tela 320x200 própria (vfb_vrPushCanvas320/vfb_vrPopCanvas trocam buffer, escala e retângulos; screen_enableGPU(false) durante o desenho), converte com a paleta do PDA (lpalette_vrBuildRgba, sem mexer na paleta da tela) e entrega RGBA por tfe_xr_set_pda_image. O XR sobe essa imagem numa textura 320x200 usada na face do tablet e na camada da janela (sem chave de preto). A paleta do mundo não é mais trocada ao abrir/fechar.
- Com o tablet aberto: gatilho e A só clicam no tablet (tiro e "usar" bloqueados no player.cpp); andar continua; script da fase, efeitos de visão e de paleta continuam rodando.
- Botão próprio do tablet de missões (ação XRA_PDA, "TABLET MISSÃO"), padrão Y sozinho; migração única (pda_bind_done) coloca no Y quando ele estava livre. O botão de mapa (padrão GE+Y) agora só cuida do mapa.
- Mapa: com o mapa aberto, cada novo aperto afasta o zoom (x0,6, mostra mais mapa) duas vezes; o aperto seguinte fecha (tfe_df_vr_map_zoom_out em automap.cpp).
- Seta do jogador no mapa (holograma e tablet) gira com a rotação real do corpo/cabeça: yaw do jogo + tfe_xr_head_yaw_units().
- versionCode 80.

## v0.33 (07/11/2026, gerada sem teste no headset)
- Tablet de missões sem pausa: abrir o PDA (objetivos/mapa) não pausa mais a missão nem o som; o mundo continua sendo desenhado por trás (mission.cpp desenha drawWorld/weapon_draw com a paleta do quadro e depois a paleta do PDA; pda.cpp pula pauseLevelSound e a troca de renderizador em VR). Ao fechar, a paleta do quadro é restaurada. Inimigos continuam agindo enquanto o tablet está aberto.
- Tradução PT-BR, fase 1 (idioma "pt" nas opções de acessibilidade do TFE é a chave):
  - Legendas de cutscenes: Captions/subtitles-pt.txt (207 linhas), legendas de cutscene e de jogo ligadas por padrão.
  - Mensagens do jogo: Translation/pt/TEXT.MSG (Latin-1) carregado por hud_translatedFile; fonte GLOWING.FNT ampliada com acentos (tools/translate/glowing_pt.py).
  - Mensagens do TFE: Translation/pt/TfeMessages.txt.
  - Opção IDIOMA DO JOGO na página VR2 (pt/en, recarrega legendas). Migração única (langDefaultDone) liga pt na primeira execução.
- Botões remapeáveis: na tela de controles cada função de botão é clicável (destaque ao apontar). Clicar abre a lista de funções (NADA, USAR / ABRIR, PULAR, AGACHAR, LANTERNA, MAPA E PDA, VISÃO NOTURNA, MÁSCARA DE GÁS, BOTAS DE GELO, ARMA ANTERIOR, GUARDAR ARMA, SALVAR RÁPIDO, CARREGAR RÁPIDO); escolher com A ou gatilho grava em bind_* no tfe_vr.ini. Botão PADRÃO restaura o mapa original. Slots: A/B/X/Y sozinhos, com garra do mesmo lado e com a outra garra; gatilho esquerdo e G+gatilho esquerdo; clique R3.
- A funciona como clique nos menus (além do gatilho).
- Próximas fases da tradução: textos desenhados em imagens LFD (briefings DFBRIEF.LFD das 14 missões, objetivos do PDA, botões do PDA, MENU.LFD/AGENTMNU, menu de pausa) redesenhados com a fonte do jogo; menus ImGui do TFE.
- versionCode 79.

## v0.32 (07/11/2026, gerada sem teste no headset)
- Faixa de armas ao trocar pelo analógico direito: ao inclinar o analógico para cima/baixo aparece, ao lado da mão direita e virada para o olho, uma coluna com a arma atual no meio (disco maior), a próxima em cima (seta para cima) e a anterior embaixo (seta para baixo); a cada troca a coluna desliza 0,16 s para a nova arma e o ícone atual dá um pulso. Some 1,2 s depois da última troca (ou ao soltar). Usa os modelos voxel pequenos da roleta.
- Tela de controles: saiu o gesto "2 mãos: mira firme" (função removida na 0.29).
- versionCode 78.
