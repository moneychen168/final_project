# Mini Chess AI 報告

## 一、 狀態評估函數

評估函數定義在 `src/games/minichess/state.cpp` 的 `evaluate()` 中，從當前玩家的角度回傳分數，分數越高代表對自己越有利。

### 1. 棋子分值

| 棋子 | 分值 |
|------|------|
| 兵（Pawn） | 20 |
| 車（Rook） | 60 |
| 馬（Knight） | 70 |
| 象（Bishop） | 80 |
| 后（Queen） | 200 |
| 王（King） | 1000 |

### 2. 棋子位置加成（Piece-Square Table）

每種棋子在棋盤不同位置有不同的加分：
- **兵**：越靠近對方底線分數越高，鼓勵推進與升變
- **馬 / 象**：靠近中央加分，邊角扣分
- **王**：開局期間留在角落較安全，加分較高

### 3. 國王威脅加成（King Tropism）

己方棋子距離敵方國王越近，加分越多，鼓勵 AI 主動包圍敵王、製造威脅。

$$加分 = 權重[棋子類型] \times (3 - 切比雪夫距離), \quad 距離 \leq 2$$

各棋子威脅權重：車=3、馬=3、象=2、后=5

### 4. 兵的推進加成

每前進一格加 2 分，鼓勵兵的推進。

### 5. 行動力加成（Mobility）

$$加分 = 2 \times （己方合法棋步數 - 對手合法棋步數）$$

行動力多代表棋形更活，選擇更多。

### 6. 最終評估公式

$$分數 = 己方總分 - 對方總分 + 行動力加成$$

---

## 二. 搜尋演算法

### 1. Negamax

使用 Negamax 框架統一雙方視角：子節點的分數取負值即為父節點的分數。

```
negamax(狀態, 深度):
    if 深度 == 0: return 評估(狀態)
    best = -無限大
    for 棋步 in 合法棋步:
        分數 = -negamax(下一狀態, 深度 - 1)
        best = max(best, 分數)
    return best
```

### 2. Alpha-Beta 剪枝

在 Negamax 基礎上加入 alpha-beta 剪枝。維護兩個邊界：
- **alpha**：目前已知己方最高分
- **beta**：對手會允許的最高分上限

當 `alpha >= beta` 時，對手不會走到這個分支，直接剪掉不用繼續搜尋，大幅減少節點數。

```
if alpha >= beta:
    break  # beta 剪枝
```

### 3 主要變化搜尋（PVS）

Alpha-Beta 的進一步優化。因為有做棋步排序，所以第一個被搜尋到的棋步高機率會是最佳的，因此第一個棋步用完整搜尋，後面棋步用比較窄的窗口快速測試：

1. 第一個子節點用**完整窗口** `[-beta, -alpha]` 搜尋
2. 後續子節點先用**零窗口** `[-alpha-1, -alpha]` 快速驗證
3. 若零窗口搜尋到更好的，才重新用完整窗口搜尋

```
if 第一個子節點:
    分數 = -pvs(下一狀態, 深度-1, -beta, -alpha)
else:
    分數 = -pvs(下一狀態, 深度-1, -alpha-1, -alpha)  # 零窗口驗證
    if alpha < 分數 < beta:
        分數 = -pvs(下一狀態, 深度-1, -beta, -alpha)  # 重新完整搜尋
```

PVS 在大多數情況下可以跳過重搜，比純 Alpha-Beta 更快。

### 4. 靜止搜尋（Quiescence Search）

在搜尋深度歸零時，若直接評估盤面可能因為「剛吃完子還未反吃」而高估/低估，因此靜止搜尋繼續展開所有**吃子**和**升變**棋步，直到盤面「安靜」再評估，解決 Horizon Effect 問題。

```
靜止搜尋(狀態, alpha, beta):
    stand_pat = 評估(狀態)
    if stand_pat >= beta: return beta
    alpha = max(alpha, stand_pat)
    for 棋步 in 吃子/升變棋步:
        分數 = -靜止搜尋(下一狀態, -beta, -alpha)
        if 分數 >= beta: return beta
        alpha = max(alpha, 分數)
    return alpha
```

預設靜止搜尋深度為 4。

---

## 三. 棋步排序

Alpha-Beta 剪枝的效果和棋步搜尋順序有很大關係。如果好的棋步越早被搜尋到，就越容易提早剪枝。

所以我加入了簡單的 move ordering，讓比較有希望的棋步排在前面。排序時主要考慮:

| 優先級 | 類型 | 說明 |
|--------|------|------|
| 1 | 吃王 | 直接獲勝，最高優先 |
| 2 | 吃子 | 用低價值棋子吃高價值棋子優先 |
| 3 | 升變 | 兵升變為后 |
| 4 | 中央控制 | 目標格靠近棋盤中心加分|

其中吃子的排序有參考 MVV-LVA 的概念，也就是 Most Valuable Victim - Least Valuable Attacker。簡單來說，就是比較希望用低價值棋子去吃高價值棋子

---

## 四、 反覆加深搜尋（Iterative Deepening）

由 UBGI 協定層控制，從深度 1 開始依序搜尋到時間用完。每完成一層都輸出當前最佳棋步，確保時間到期時一定有答案可以輸出。

---

## 五、 重複局面偵測

使用 **Zobrist Hashing** 對每個盤面產生 64-bit 雜湊值，並透過 GameHistory 記錄歷史局面出現次數。當同一局面出現 3 次或以上，判定為平局，避免重複循環。

---

## 六、Submission 註冊

除了搜尋本身，我也新增了 submission.cpp 和 submission.hpp，並且在 registry.hpp 裡註冊 submission 這個演算法。

submission 本身主要是呼叫已經完成的 MiniMax 搜尋。這樣做的好處是 GUI 或對戰程式可以直接選到我的 AI，而不是還停留在原本的預設演算法。

## 七、 結論與成果

| 功能 | 實作狀況 |
|------|----------|
| 狀態評估函數 | ✅ 棋子分值 + 位置加成 + 國王威脅 + 行動力 |
| Minimax | ✅ Negamax 框架 |
| Alpha-Beta 剪枝 | ✅ |
| 主要變化搜尋（PVS） | ✅ |
| 靜止搜尋 | ✅ |
| 棋步排序 | ✅ MVV-LVA + 中央控制 |
| 反覆加深搜尋 | ✅ |
| 重複局面偵測 | ✅ Zobrist Hash |

實作所有基本演算法後，AI 可在時間限制內，成功擊敗 Weak MiniMax 與 Strong MiniMax 基準程式。

## 八、GitHub Version Control

本專案使用 GitHub 進行版本管理。
GitHub Repository: https://github.com/moneychen168/final_project.git
