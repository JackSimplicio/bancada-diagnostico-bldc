# Intertravamento e ensaio sob carga – primeiros ensaios (19/11/2026)

Verificação da sequência de estados da bancada: diagnóstico aprovado → ensaio sob carga → TRAVADO → liberação pelo botão físico → AGUARDANDO.

- Para agilizar a sequência, o diagnóstico estático foi feito com **três resistores iguais em estrela** no lugar do motor (uma extremidade comum e a outra em cada contato de relé). Depois da aprovação, o motor foi ligado ao *driver* em 24 V e a carga foi aplicada pelo freio, incluindo um ensaio com o eixo travado pelas pinças antes da partida.
- `tela_*.jpeg`: capturas da página web durante os ensaios. `video_*.mp4`: gravações da operação. `ensaio_bancada 92.csv`: registro de um ensaio sob carga.

**Atenção:** nesses ensaios faltava a ligação de referência entre o negativo da fonte de 24 V e o GND do circuito de medição. Por isso a **tensão do barramento registrada (entre 0,3 e 1,8 V nas telas e no CSV) não é válida**, e as correntes são tratadas apenas como indicativas. A sequência de estados não foi afetada, porque o firmware decide só pela corrente. Os ensaios foram repetidos com a ligação de referência em `2026-11-26_ensaio_sob_carga_com_referencia_e_fase_aberta/`.
