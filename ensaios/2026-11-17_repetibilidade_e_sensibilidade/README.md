# Ensaios de 17/11/2026 – repetibilidade e sensibilidade do diagnóstico estático

**Configuração:** motor KTC42BLS04-J desconectado do driver, medição a 4 fios (Kelvin), limite de desequilíbrio de 5 %, temperatura ambiente, sem correção de temperatura.

**Condições ensaiadas:** motor sadio e resistores de valor conhecido inseridos **em série na fase W**, para simular fase degradada (conexão com resistência adicional).

| Pasta em `brutos/` | Resistência inserida | Composição |
|---|---|---|
| `Ensaio_Motor_Sadio` | – | – |
| `Ensaio_Res_0.1_Fase_W` | 0,1 Ω | 1 × 0,1 Ω |
| `Ensaio_Res_0.067_Fase_W` | 0,0667 Ω | 0,1 Ω ∥ 0,2 Ω |
| `Ensaio_Res_0.028_Fase_W` | 0,0286 Ω | 3 × 0,1 Ω ∥ 0,2 Ω |
| `Ensaio_Res_0.015_Fase_W` | 0,0154 Ω | 6 × 0,1 Ω ∥ 0,2 Ω |
| `Ensaio_Res_0.00937_Fase_W` | **0,00952 Ω** | 10 × 0,1 Ω ∥ 0,2 Ω (o nome da pasta traz o valor de um cálculo manual anterior; o valor correto é 0,00952 Ω) |

## Resultados

| Condição | Execuções (válidas) | R média (Ω) | Desequilíbrio (%) média ± dp (máx.) | Reprovados (> 5 %) | Acréscimo medido na fase W (Ω) |
|---|---|---|---|---|---|
| Motor sadio | 21 (18) | 0,3153 | 2,09 ± 0,93 (3,16) | 0 | – |
| + 0,1 Ω | 7 (7) | 0,4270 | 26,38 ± 1,18 (27,46) | 7 | 0,1112 |
| + 0,0667 Ω | 9 (9) | 0,3961 | 18,98 ± 1,05 (20,10) | 9 | 0,0729 |
| + 0,0286 Ω | 8 (8) | 0,3796 | 13,55 ± 0,91 (14,96) | 8 | 0,0487 |
| + 0,0154 Ω | 9 (9) | 0,3709 | 9,35 ± 0,51 (10,29) | 9 | 0,0326 |
| + 0,00952 Ω | 10 (10) | 0,3730 | 5,63 ± 0,82 (6,59) | 6 | 0,0199 |

- **Acréscimo medido na fase W** = média de V–W e W–U menos U–V **da mesma execução**, o que cancela variações de montagem entre as condições.
- Arquivos completos: `resumo_por_execucao.csv` (cada execução, com status) e `resumo_por_condicao.csv`.

## Execuções descartadas (critério declarado)

Do motor sadio, foram executadas 21 medições:
- **Execução 3:** sem registro de dados.
- **Execuções 2 e 4:** resistência média mais de 5 % acima da mediana do conjunto (0,365 Ω e 0,406 Ω), atribuída a interferência mecânica nos contatos durante o manuseio. O desequilíbrio dessas execuções (2,8 % e 2,7 %) ficou dentro da faixa das demais.

Os arquivos brutos descartados **foram mantidos** em `brutos/` para rastreabilidade.

## Limitação conhecida

A montagem em protoboard e bornes mantém parte das soldas e conexões dentro da malha de leitura. Por isso o **valor absoluto** da resistência varia com a montagem (0,342 Ω em 09/11 e 0,315 Ω em 17/11), enquanto o **desequilíbrio entre pares**, critério de aprovação adotado, é medido com a mesma referência para os três pares na mesma montagem.
