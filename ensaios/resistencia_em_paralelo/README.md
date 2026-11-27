# Sensibilidade a curtos parciais – resistência em paralelo com um par

Resistores de 10, 7, 5 e 1 Ω (valores nominais) ligados em paralelo com um par de fases do motor KTC42BLS04-J, simulando curtos parciais, além de um curto com *jumper*. Cinco execuções por condição, com alternância do par afetado entre as execuções. Diagnóstico estático pelo método Kelvin, limite de reprovação de 5 %.

| Arquivo | Conteúdo |
|---|---|
| `brutos/` | CSVs exportados pela bancada, um por execução |
| `resumo_por_execucao.csv` | Resistência de cada par, média e desequilíbrio por execução |
| `resumo_por_condicao.csv` | Mediana, mínimo, máximo e número de reprovações por condição, com o valor previsto pelo modelo |

**Modelo:** motor em estrela com resistência de fase R_f = 0,172 Ω e R_p em paralelo com o par A–B: R_AB = 2R_f·R_p/(2R_f + R_p) e R_AC = R_BC = R_f + R_f(R_p + R_f)/(R_p + 2R_f). O curto foi modelado com R_p = 0,008 Ω, valor estimado do *jumper* com os contatos.

**Resultado:** todas as execuções com 10 Ω foram aprovadas e todas com 7 Ω ou menos foram reprovadas; o limiar de detecção fica entre 7 e 10 Ω. O medido ficou de 1 a 6 pontos percentuais acima do modelo, diferença da ordem do desequilíbrio natural do motor sadio, que o modelo não inclui.

**Observação:** uma execução com 7 Ω (arquivo `ensaio_bancada (10).csv`) deu 22,1 %, com o par U–V em 0,4139 Ω, atribuída a mau contato. Ela consta dos brutos e conta como reprovação, mas foi excluída do mínimo e do máximo do resumo por condição.
