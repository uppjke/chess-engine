const boardEl = document.getElementById('board');
const resetBtn = document.getElementById('resetBtn');
const undoBtn = document.getElementById('undoBtn');
const redoBtn = document.getElementById('redoBtn');
const flipBtn = document.getElementById('flipBtn');
const editBtn = document.getElementById('editBtn');
const engineMoveBtn = document.getElementById('engineMoveBtn');
const paletteEl = document.getElementById('palette');
const applyEditBtn = document.getElementById('applyEditBtn');
const statusEl = document.getElementById('status');
const moveTimeEl = document.getElementById('moveTime');
const humanSideEl = document.getElementById('humanSide');
const sideToMoveEl = document.getElementById('sideToMove');
const autoMoveEl = document.getElementById('autoMove');
const castleKEl = document.getElementById('castleK');
const castleQEl = document.getElementById('castleQ');
const castlekEl = document.getElementById('castlek');
const castleqEl = document.getElementById('castleq');
const moveListEl = document.getElementById('moveList');
const gameStatusEl = document.getElementById('gameStatus');
const turnStatusEl = document.getElementById('turnStatus');
const engineStatusEl = document.getElementById('engineStatus');

const PIECE_TO_SVG = {
  P: 'pawn-w',
  N: 'knight-w',
  B: 'bishop-w',
  R: 'rook-w',
  Q: 'queen-w',
  K: 'king-w',
  p: 'pawn-b',
  n: 'knight-b',
  b: 'bishop-b',
  r: 'rook-b',
  q: 'queen-b',
  k: 'king-b'
};

const PALETTE = ['P','N','B','R','Q','K','p','n','b','r','q','k'];
let selectedPalette = 'P';

let board = new Array(64).fill('.');
let flipped = false;
let editMode = false;
let selectedSquare = null;
let legalMoves = new Set();
let history = [];
let historyIndex = 0;
let moveList = [];
let sideToMove = 'w';
let castling = { K: true, Q: true, k: true, q: true };
let epSquare = '-';
let halfmove = 0;
let fullmove = 1;
let lastMove = null;
let positionHistory = new Map();
let previewIndex = null;
let liveSnapshot = null;

let dragData = null;
let dragHandled = false;
let ws;
let reconnectTimer = null;
let engineBusy = false;
let legalMovesFresh = false;

function engineSide() {
  return humanSideEl.value;
}

function userSide() {
  return engineSide() === 'w' ? 'b' : 'w';
}

function updateOrientation() {
  flipped = engineSide() === 'b';
  renderBoard();
}

function updateTopStatus() {
  if (turnStatusEl) {
    turnStatusEl.textContent = `Ход: ${sideToMove === 'w' ? 'Белые' : 'Чёрные'}`;
  }
  if (engineStatusEl) {
    engineStatusEl.textContent = `Движок: ${engineSide() === 'w' ? 'Белые' : 'Чёрные'}`;
  }
}

function setConnectionStatus(connected) {
  if (!statusEl) return;
  statusEl.textContent = connected ? 'Подключено' : 'Отключено';
  statusEl.dataset.state = connected ? 'ok' : 'off';
}

function sendUci(command) {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  ws.send(JSON.stringify({ type: 'uci', command }));
}

function maybeAutoMove() {
  if (!autoMoveEl || !autoMoveEl.checked) return;
  if (editMode) return;
  if (sideToMove !== engineSide()) return;
  if (engineBusy) return;
  if (lastMove && lastMove.side === engineSide()) return;
  requestEngineMove();
}

function indexToCoord(index) {
  const file = index % 8;
  const rank = Math.floor(index / 8);
  return { file, rank };
}

function coordToIndex(file, rank) {
  return rank * 8 + file;
}

function indexToUci(index) {
  const { file, rank } = indexToCoord(index);
  return String.fromCharCode(97 + file) + String.fromCharCode(49 + rank);
}

function getPieceSrc(piece) {
  const name = PIECE_TO_SVG[piece];
  return name ? `/pieces-svg/${name}.svg` : null;
}

function choosePromotion() {
  const choice = window.prompt('Выберите превращение: q, r, b, n', 'q');
  if (!choice) return null;
  const c = choice.trim().toLowerCase();
  if (!['q', 'r', 'b', 'n'].includes(c)) return null;
  return c;
}

function findKing(side) {
  const king = side === 'w' ? 'K' : 'k';
  for (let i = 0; i < 64; i++) {
    if (board[i] === king) return i;
  }
  return -1;
}

function isSquareAttacked(sq, bySide) {
  const { file, rank } = indexToCoord(sq);
  const pawn = bySide === 'w' ? 'P' : 'p';
  const dir = bySide === 'w' ? -1 : 1;
  const pawnRank = rank + dir;
  if (pawnRank >= 0 && pawnRank <= 7) {
    for (const df of [-1, 1]) {
      const nf = file + df;
      if (nf < 0 || nf > 7) continue;
      const idx = coordToIndex(nf, pawnRank);
      if (board[idx] === pawn) return true;
    }
  }

  const knight = bySide === 'w' ? 'N' : 'n';
  const knightOffsets = [[1,2],[2,1],[2,-1],[1,-2],[-1,-2],[-2,-1],[-2,1],[-1,2]];
  for (const [df, dr] of knightOffsets) {
    const nf = file + df;
    const nr = rank + dr;
    if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
    if (board[coordToIndex(nf, nr)] === knight) return true;
  }

  const bishop = bySide === 'w' ? 'B' : 'b';
  const rook = bySide === 'w' ? 'R' : 'r';
  const queen = bySide === 'w' ? 'Q' : 'q';
  const king = bySide === 'w' ? 'K' : 'k';

  const diagDirs = [[1,1],[1,-1],[-1,1],[-1,-1]];
  for (const [df, dr] of diagDirs) {
    let nf = file + df;
    let nr = rank + dr;
    while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
      const p = board[coordToIndex(nf, nr)];
      if (p !== '.') {
        if (p === bishop || p === queen) return true;
        break;
      }
      nf += df;
      nr += dr;
    }
  }

  const orthoDirs = [[1,0],[-1,0],[0,1],[0,-1]];
  for (const [df, dr] of orthoDirs) {
    let nf = file + df;
    let nr = rank + dr;
    while (nf >= 0 && nf <= 7 && nr >= 0 && nr <= 7) {
      const p = board[coordToIndex(nf, nr)];
      if (p !== '.') {
        if (p === rook || p === queen) return true;
        break;
      }
      nf += df;
      nr += dr;
    }
  }

  const kingDirs = [[1,1],[1,0],[1,-1],[0,1],[0,-1],[-1,1],[-1,0],[-1,-1]];
  for (const [df, dr] of kingDirs) {
    const nf = file + df;
    const nr = rank + dr;
    if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
    if (board[coordToIndex(nf, nr)] === king) return true;
  }

  return false;
}

function isKingInCheck(side) {
  const kingSq = findKing(side);
  if (kingSq === -1) return false;
  const attacker = side === 'w' ? 'b' : 'w';
  return isSquareAttacked(kingSq, attacker);
}

function isInsufficientMaterial() {
  let wN = 0, wB = 0, wOther = 0, wBsq = -1;
  let bN = 0, bB = 0, bOther = 0, bBsq = -1;
  for (let sq = 0; sq < 64; sq++) {
    const p = board[sq];
    if (p === '.' || p === 'K' || p === 'k') continue;
    if (p === 'N') wN++;
    else if (p === 'B') { wB++; wBsq = sq; }
    else if (p === p.toUpperCase()) wOther++;
    else if (p === 'n') bN++;
    else if (p === 'b') { bB++; bBsq = sq; }
    else bOther++;
  }
  if (wOther > 0 || bOther > 0) return false;
  const wMinor = wN + wB, bMinor = bN + bB;
  if (wMinor === 0 && bMinor === 0) return true;
  if (wMinor === 0 && bMinor === 1) return true;
  if (wMinor === 1 && bMinor === 0) return true;
  if (wB === 1 && bB === 1 && wN === 0 && bN === 0) {
    const file = sq => sq & 7, rank = sq => sq >> 3;
    const wLight = (file(wBsq) + rank(wBsq)) % 2 === 0;
    const bLight = (file(bBsq) + rank(bBsq)) % 2 === 0;
    if (wLight === bLight) return true;
  }
  return false;
}

function updateGameStatus() {
  if (!gameStatusEl) return;
  let text = 'ОК';
  let state = 'ok';
  if (halfmove >= 100) {
    text = 'Ничья (50 ходов)';
    state = 'warn';
  } else if (isThreefold()) {
    text = 'Ничья (3 повтора)';
    state = 'warn';
  } else if (isInsufficientMaterial()) {
    text = 'Ничья (недостаточно фигур)';
    state = 'warn';
  } else if (legalMoves.size === 0) {
    if (isKingInCheck(sideToMove)) {
      text = 'Мат';
      state = 'danger';
    } else {
      text = 'Пат';
      state = 'warn';
    }
  } else if (isKingInCheck(sideToMove)) {
    text = 'Шах';
    state = 'warn';
  }
  gameStatusEl.textContent = text;
  gameStatusEl.dataset.state = state;
  if (legalMovesFresh) {
    const inCheck = isKingInCheck(sideToMove);
    if (legalMoves.size === 0 && inCheck) {
      updateLastMoveSuffix('#');
    } else if (inCheck) {
      updateLastMoveSuffix('+');
    } else {
      updateLastMoveSuffix('');
    }
  }
  updateTopStatus();
}

function normalizeSanSuffix(san) {
  return san.replace(/[+#]+$/g, '');
}

function updateLastMoveSuffix(suffix) {
  if (!moveList.length) return;
  const lastIndex = moveList.length - 1;
  const base = normalizeSanSuffix(moveList[lastIndex]);
  const next = suffix ? `${base}${suffix}` : base;
  if (moveList[lastIndex] === next) return;
  moveList[lastIndex] = next;
  renderMoveList();
}

function renderBoard() {
  boardEl.innerHTML = '';
  const indices = [];
  for (let r = 0; r < 8; r++) {
    for (let f = 0; f < 8; f++) {
      const rank = flipped ? r : 7 - r;
      const file = flipped ? 7 - f : f;
      indices.push(coordToIndex(file, rank));
    }
  }

  indices.forEach((idx) => {
    const { file, rank } = indexToCoord(idx);
    const square = document.createElement('div');
    const isLight = (file + rank) % 2 === 1;
    square.className = `square ${isLight ? 'light' : 'dark'}`;
    square.dataset.index = idx;

    if (selectedSquare === idx) square.classList.add('selected');
    if (lastMove && lastMove.side !== sideToMove) {
      if (idx === lastMove.from || idx === lastMove.to) {
        square.classList.add('last-move');
      }
    }

    const uciList = [...legalMoves].filter(m => m.startsWith(indexToUci(idx)));
    if (uciList.length > 0) square.classList.add('legal');

    const piece = board[idx];
    const src = getPieceSrc(piece);
    square.textContent = '';
    if (src) {
      const img = document.createElement('img');
      img.src = src;
      img.alt = piece;
      img.className = 'piece';
      square.appendChild(img);
    }
    square.addEventListener('click', onSquareClick);
    square.addEventListener('dragstart', (e) => onSquareDragStart(e, idx, piece));
    square.addEventListener('dragend', () => onSquareDragEnd(idx));
    square.addEventListener('dragover', onSquareDragOver);
    square.addEventListener('dragleave', onSquareDragLeave);
    square.addEventListener('drop', onSquareDrop);
    square.draggable = canDragPiece(piece);
    boardEl.appendChild(square);
  });
}

function canDragPiece(piece) {
  if (piece === '.') return false;
  if (editMode) return true;
  const isWhite = piece === piece.toUpperCase();
  return (userSide() === 'w' && isWhite) || (userSide() === 'b' && !isWhite);
}

function onSquareDragStart(e, idx, piece) {
  if (previewIndex !== null) {
    exitPreview();
    e.preventDefault();
    return;
  }
  if (!canDragPiece(piece)) {
    e.preventDefault();
    return;
  }
  dragData = { source: 'board', fromIdx: idx, piece };
  dragHandled = false;
  e.dataTransfer.effectAllowed = 'move';
  e.dataTransfer.setData('text/plain', `${idx}:${piece}`);

  const dragImg = document.createElement('img');
  const src = getPieceSrc(piece);
  if (src) {
    dragImg.src = src;
    dragImg.width = 48;
    dragImg.height = 48;
    dragImg.style.position = 'absolute';
    dragImg.style.top = '-1000px';
    document.body.appendChild(dragImg);
    e.dataTransfer.setDragImage(dragImg, 24, 24);
    setTimeout(() => dragImg.remove(), 0);
  }

  e.currentTarget.style.opacity = '0.4';
  if (!editMode) selectedSquare = idx;
}

function onSquareDragEnd(fromIdx) {
  if (!editMode) return;
  if (!dragData || dragData.source !== 'board') return;
  if (dragHandled) {
    dragData = null;
    return;
  }
  board[fromIdx] = '.';
  dragData = null;
  renderBoard();
}

function onSquareDragOver(e) {
  if (previewIndex !== null) return;
  e.preventDefault();
  e.currentTarget.classList.add('drag-over');
  e.dataTransfer.dropEffect = editMode ? 'copy' : 'move';
}

function onSquareDragLeave(e) {
  e.currentTarget.classList.remove('drag-over');
}

function onSquareDrop(e) {
  if (previewIndex !== null) {
    exitPreview();
    return;
  }
  e.preventDefault();
  e.currentTarget.classList.remove('drag-over');
  document.querySelectorAll('.square').forEach(sq => sq.style.opacity = '1');
  const toIdx = Number(e.currentTarget.dataset.index);
  const data = dragData;
  dragHandled = true;
  dragData = null;

  if (!data) return;

  if (editMode) {
    if (data.source === 'board') {
      if (data.fromIdx === toIdx) return;
      board[toIdx] = data.piece;
      board[data.fromIdx] = '.';
    } else if (data.source === 'palette') {
      board[toIdx] = data.piece;
    }
    renderBoard();
    return;
  }

  if (sideToMove !== userSide()) return;
  if (data.source !== 'board') return;

  const move = indexToUci(data.fromIdx) + indexToUci(toIdx);
  if (isPromotionMove(data.fromIdx, toIdx)) {
    const promo = choosePromotion();
    if (!promo) return;
    if (legalMoves.has(move + promo)) {
      applyMove(move + promo);
    }
  } else if (legalMoves.has(move)) {
    applyMove(move);
  }
  selectedSquare = null;
  renderBoard();
}

function onSquareClick(e) {
  if (previewIndex !== null) {
    exitPreview();
    return;
  }
  const idx = Number(e.currentTarget.dataset.index);
  if (editMode) {
    board[idx] = selectedPalette;
    renderBoard();
    return;
  }

  if (sideToMove !== userSide()) return;

  const piece = board[idx];
  if (selectedSquare === null) {
    if (piece === '.') return;
    const isWhite = piece === piece.toUpperCase();
    if ((sideToMove === 'w' && !isWhite) || (sideToMove === 'b' && isWhite)) return;
    selectedSquare = idx;
  } else {
    const move = indexToUci(selectedSquare) + indexToUci(idx);
    if (isPromotionMove(selectedSquare, idx)) {
      const promo = choosePromotion();
      if (!promo) {
        selectedSquare = null;
        renderBoard();
        return;
      }
      if (legalMoves.has(move + promo)) {
        applyMove(move + promo);
      }
    } else if (legalMoves.has(move)) {
      applyMove(move);
    }
    selectedSquare = null;
  }
  renderBoard();
}

function isPromotionMove(from, to) {
  const piece = board[from];
  if (piece.toLowerCase() !== 'p') return false;
  const { rank } = indexToCoord(to);
  return (piece === 'P' && rank === 7) || (piece === 'p' && rank === 0);
}

function parseFen(fen) {
  const parts = fen.split(' ');
  const boardPart = parts[0];
  const stm = parts[1];
  const castlingPart = parts[2] || '-';
  const epPart = parts[3] || '-';
  const halfmovePart = parts[4] || '0';
  const fullmovePart = parts[5] || '1';
  const rows = boardPart.split('/');
  const result = new Array(64).fill('.');
  for (let r = 7; r >= 0; r--) {
    const row = rows[7 - r];
    let f = 0;
    for (const ch of row) {
      if (ch >= '1' && ch <= '8') {
        f += Number(ch);
      } else {
        result[coordToIndex(f, r)] = ch;
        f += 1;
      }
    }
  }
  board = result;
  sideToMove = stm || 'w';
  castling = { K: false, Q: false, k: false, q: false };
  if (castlingPart.includes('K')) castling.K = true;
  if (castlingPart.includes('Q')) castling.Q = true;
  if (castlingPart.includes('k')) castling.k = true;
  if (castlingPart.includes('q')) castling.q = true;
  epSquare = epPart;
  halfmove = Number(halfmovePart);
  fullmove = Number(fullmovePart);
  sideToMoveEl.value = sideToMove;
  castleKEl.checked = castling.K;
  castleQEl.checked = castling.Q;
  castlekEl.checked = castling.k;
  castleqEl.checked = castling.q;
  positionHistory.clear();
  recordPosition();
}

function boardToFen() {
  let fen = '';
  for (let r = 7; r >= 0; r--) {
    let empty = 0;
    for (let f = 0; f < 8; f++) {
      const piece = board[coordToIndex(f, r)];
      if (piece === '.') {
        empty++;
      } else {
        if (empty > 0) {
          fen += empty;
          empty = 0;
        }
        fen += piece;
      }
    }
    if (empty > 0) fen += empty;
    if (r > 0) fen += '/';
  }
  let castleStr = '';
  if (castling.K) castleStr += 'K';
  if (castling.Q) castleStr += 'Q';
  if (castling.k) castleStr += 'k';
  if (castling.q) castleStr += 'q';
  if (castleStr === '') castleStr = '-';
  fen += ` ${sideToMove} ${castleStr} ${epSquare} ${halfmove} ${fullmove}`;
  return fen;
}

function snapshot() {
  return {
    board: [...board],
    sideToMove,
    castling: { ...castling },
    epSquare,
    halfmove,
    fullmove,
    lastMove: lastMove ? { ...lastMove } : null,
    posHist: new Map(positionHistory)
  };
}

function restoreSnapshot(snap) {
  board = [...snap.board];
  sideToMove = snap.sideToMove;
  castling = { ...snap.castling };
  epSquare = snap.epSquare;
  halfmove = snap.halfmove;
  fullmove = snap.fullmove;
  lastMove = snap.lastMove ? { ...snap.lastMove } : null;
  sideToMoveEl.value = sideToMove;
  castleKEl.checked = castling.K;
  castleQEl.checked = castling.Q;
  castlekEl.checked = castling.k;
  castleqEl.checked = castling.q;
  positionHistory = new Map(snap.posHist || []);
  updateGameStatus();
  updateTopStatus();
}

function recordPosition() {
  const key = boardToFen().split(' ').slice(0, 4).join(' ');
  const cnt = positionHistory.get(key) || 0;
  positionHistory.set(key, cnt + 1);
}

function isThreefold() {
  const key = boardToFen().split(' ').slice(0, 4).join(' ');
  return (positionHistory.get(key) || 0) >= 3;
}

function moveToSan(uci) {
  const from = uci.slice(0, 2);
  const to = uci.slice(2, 4);
  const promo = uci.length > 4 ? uci[4].toUpperCase() : null;
  const fromIdx = coordToIndex(from.charCodeAt(0) - 97, from.charCodeAt(1) - 49);
  const toIdx = coordToIndex(to.charCodeAt(0) - 97, to.charCodeAt(1) - 49);
  const piece = board[fromIdx];
  const target = board[toIdx];

  const isPawn = piece.toLowerCase() === 'p';
  const isCapture = target !== '.' || (isPawn && from[0] !== to[0]);
  const isCastle = piece.toLowerCase() === 'k' && Math.abs(fromIdx - toIdx) === 2;
  if (isCastle) return (to === 'g1' || to === 'g8') ? 'O-O' : 'O-O-O';

  const pieceLetter = isPawn ? '' : piece.toUpperCase();
  let disambiguation = '';
  if (!isPawn) {
    const sameTargets = [...legalMoves].filter(m => m.slice(2, 4) === to);
    const samePieceMoves = sameTargets.filter(m => {
      const f = coordToIndex(m.charCodeAt(0) - 97, m.charCodeAt(1) - 49);
      return board[f].toLowerCase() === piece.toLowerCase() && f !== fromIdx;
    });
    if (samePieceMoves.length > 0) {
      const fromFile = from[0];
      const fromRank = from[1];
      const fileConflict = samePieceMoves.some(m => m[0] === fromFile);
      const rankConflict = samePieceMoves.some(m => m[1] === fromRank);
      if (fileConflict && rankConflict) disambiguation = fromFile + fromRank;
      else if (fileConflict) disambiguation = fromRank;
      else disambiguation = fromFile;
    }
  }

  let san = pieceLetter + disambiguation;
  if (isCapture) san += 'x';
  san += to;
  if (promo) san += '=' + promo;
  return san;
}

function pushHistory(sanMove = null) {
  if (historyIndex < history.length - 1) {
    history = history.slice(0, historyIndex + 1);
    moveList = moveList.slice(0, historyIndex);
  }
  history.push(snapshot());
  historyIndex = history.length - 1;
  if (sanMove) moveList.push(sanMove);
  renderMoveList();
}

function renderMoveList() {
  if (!moveListEl) return;
  const nearBottom = moveListEl.scrollTop + moveListEl.clientHeight >= moveListEl.scrollHeight - 24;
  const lastIndex = moveList.length - 1;
  const currentIndex = previewIndex !== null ? previewIndex : historyIndex - 1;
  const rows = [];
  for (let i = 0; i < moveList.length; i += 2) {
    const moveNo = Math.floor(i / 2) + 1;
    const wIndex = i;
    const bIndex = i + 1;
    const wMove = moveList[wIndex] || '';
    const bMove = moveList[bIndex] || '';
    const wClass = wIndex === lastIndex ? 'move-cell is-last' : 'move-cell';
    const bClass = bIndex === lastIndex ? 'move-cell is-last' : 'move-cell';
    const wCurrent = wIndex === currentIndex ? ' is-current' : '';
    const bCurrent = bIndex === currentIndex ? ' is-current' : '';
    rows.push(`<div class="move-row"><span class="move-no">${moveNo}.</span><span class="${wClass}${wCurrent}" data-move-index="${wIndex}">${wMove}</span><span class="${bClass}${bCurrent}" data-move-index="${bIndex}">${bMove}</span></div>`);
  }
  moveListEl.innerHTML = rows.join('');
  if (nearBottom) moveListEl.scrollTop = moveListEl.scrollHeight;
}

function enterPreview(moveIndex) {
  if (moveIndex < 0) return;
  const snapIndex = moveIndex + 1;
  if (!history[snapIndex]) return;
  if (previewIndex === null) {
    liveSnapshot = snapshot();
  }
  previewIndex = moveIndex;
  restoreSnapshot(history[snapIndex]);
  renderBoard();
  renderMoveList();
}

function exitPreview() {
  if (previewIndex === null) return;
  previewIndex = null;
  if (liveSnapshot) {
    restoreSnapshot(liveSnapshot);
    liveSnapshot = null;
  }
  renderBoard();
  renderMoveList();
  syncEngine();
}

function applyMove(uci) {
  if (previewIndex !== null) exitPreview();
  const san = moveToSan(uci);
  const moverSide = sideToMove;
  const from = uci.slice(0, 2);
  const to = uci.slice(2, 4);
  const promo = uci.length > 4 ? uci[4] : null;
  const fromIdx = coordToIndex(from.charCodeAt(0) - 97, from.charCodeAt(1) - 49);
  const toIdx = coordToIndex(to.charCodeAt(0) - 97, to.charCodeAt(1) - 49);
  const piece = board[fromIdx];
  const target = board[toIdx];

  const isPawn = piece.toLowerCase() === 'p';
  const isCapture = target !== '.';
  const isEnPassant = isPawn && target === '.' && (from[0] !== to[0]);
  const isCastle = piece.toLowerCase() === 'k' && Math.abs(fromIdx - toIdx) === 2;

  if (isEnPassant) {
    const dir = sideToMove === 'w' ? -1 : 1;
    const capIdx = toIdx + dir * 8;
    board[capIdx] = '.';
  }

  if (isCastle) {
    if (to === 'g1') { board[coordToIndex(5,0)] = 'R'; board[coordToIndex(7,0)] = '.'; }
    if (to === 'c1') { board[coordToIndex(3,0)] = 'R'; board[coordToIndex(0,0)] = '.'; }
    if (to === 'g8') { board[coordToIndex(5,7)] = 'r'; board[coordToIndex(7,7)] = '.'; }
    if (to === 'c8') { board[coordToIndex(3,7)] = 'r'; board[coordToIndex(0,7)] = '.'; }
  }

  board[toIdx] = promo ? (sideToMove === 'w' ? promo.toUpperCase() : promo) : piece;
  board[fromIdx] = '.';

  epSquare = '-';
  if (isPawn) {
    const fromRank = fromIdx >> 3;
    const toRank = toIdx >> 3;
    if (Math.abs(fromRank - toRank) === 2) {
      const epRank = sideToMove === 'w' ? fromRank + 1 : fromRank - 1;
      epSquare = String.fromCharCode(97 + (fromIdx % 8)) + String.fromCharCode(49 + epRank);
    }
  }

  if (piece.toLowerCase() === 'k') {
    if (sideToMove === 'w') { castling.K = false; castling.Q = false; }
    else { castling.k = false; castling.q = false; }
  }
  if (piece.toLowerCase() === 'r') {
    if (from === 'a1') castling.Q = false;
    if (from === 'h1') castling.K = false;
    if (from === 'a8') castling.q = false;
    if (from === 'h8') castling.k = false;
  }
  if (isCapture) {
    if (to === 'a1') castling.Q = false;
    if (to === 'h1') castling.K = false;
    if (to === 'a8') castling.q = false;
    if (to === 'h8') castling.k = false;
  }

  if (isPawn || isCapture || isEnPassant) halfmove = 0; else halfmove += 1;
  if (sideToMove === 'b') fullmove += 1;

  sideToMove = sideToMove === 'w' ? 'b' : 'w';
  sideToMoveEl.value = sideToMove;
  lastMove = { from: fromIdx, to: toIdx, side: moverSide };
  recordPosition();

  pushHistory(san);
  syncEngine();
  renderBoard();
  updateGameStatus();
  maybeAutoMove();
}

function requestEngineMove() {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  if (sideToMove !== engineSide()) return;
  engineBusy = true;
  const ms = Number(moveTimeEl.value || 2000);
  sendUci(`go movetime ${ms}`);
}

function syncEngine() {
  if (!ws || ws.readyState !== WebSocket.OPEN) return;
  const fen = boardToFen();
  legalMovesFresh = false;
  sendUci(`position fen ${fen}`);
  sendUci('legal');
}

function setStartPosition() {
  parseFen('rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1');
  history = [];
  moveList = [];
  historyIndex = 0;
  lastMove = null;
  positionHistory.clear();
  recordPosition();
  pushHistory();
  sendUci('ucinewgame'); // Reset engine state
  syncEngine();
  updateOrientation();
  updateGameStatus();
  maybeAutoMove();
}

function setupPalette() {
  paletteEl.innerHTML = '';
  PALETTE.forEach((p) => {
    const item = document.createElement('div');
    item.className = 'palette-item' + (p === selectedPalette ? ' active' : '');
    const src = getPieceSrc(p);
    item.textContent = '';
    if (src) {
      const img = document.createElement('img');
      img.src = src;
      img.alt = p;
      img.className = 'piece';
      item.appendChild(img);
    }
    item.draggable = true;
    item.addEventListener('click', () => {
      selectedPalette = p;
      setupPalette();
    });
    item.addEventListener('dragstart', (e) => {
      if (!editMode) {
        e.preventDefault();
        return;
      }
      dragData = { source: 'palette', piece: p };
      e.dataTransfer.effectAllowed = 'copy';
      e.dataTransfer.setData('text/plain', `palette:${p}`);
      selectedPalette = p;
      setupPalette();
    });
    paletteEl.appendChild(item);
  });
}

function connect() {
  ws = new WebSocket('ws://localhost:3000');
  ws.onopen = () => {
    setConnectionStatus(true);
    sendUci('uci');
    sendUci('isready');
    syncEngine();
    maybeAutoMove();
  };
  ws.onclose = () => {
    setConnectionStatus(false);
    if (!reconnectTimer) {
      reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connect();
      }, 1000);
    }
  };
  ws.onerror = () => {};
  ws.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data);
      if (msg.type !== 'uci') return;
      const line = msg.line || '';
      if (line.startsWith('legal')) {
        legalMoves = new Set(line.split(' ').slice(1));
        legalMovesFresh = true;
        renderBoard();
        updateGameStatus();
        maybeAutoMove();
      }
      if (line.startsWith('bestmove')) {
        const parts = line.split(' ');
        engineBusy = false;
        if (parts[1] && parts[1] !== '0000') {
          applyMove(parts[1]);
        }
      }
    } catch (_) {
      // ignore
    }
  };
}

resetBtn.addEventListener('click', setStartPosition);
undoBtn.addEventListener('click', () => {
  exitPreview();
  if (historyIndex <= 0) return;
  historyIndex -= 1;
  restoreSnapshot(history[historyIndex]);
  moveList = moveList.slice(0, historyIndex);
  renderMoveList();
  syncEngine();
  renderBoard();
});
if (redoBtn) {
  redoBtn.addEventListener('click', () => {
    exitPreview();
    if (historyIndex >= history.length - 1) return;
    historyIndex += 1;
    restoreSnapshot(history[historyIndex]);
    renderMoveList();
    syncEngine();
    renderBoard();
  });
}
flipBtn.addEventListener('click', () => {
  exitPreview();
  flipped = !flipped;
  renderBoard();
});
editBtn.addEventListener('click', () => {
  exitPreview();
  editMode = !editMode;
  editBtn.textContent = `Редактор: ${editMode ? 'вкл' : 'выкл'}`;
  renderBoard(); // Re-render to update draggable state
});
engineMoveBtn.addEventListener('click', requestEngineMove);
if (autoMoveEl) {
  autoMoveEl.addEventListener('change', () => {
    maybeAutoMove();
  });
}
if (moveListEl) {
  moveListEl.addEventListener('click', (e) => {
    const target = e.target;
    if (!(target instanceof HTMLElement)) return;
    const idx = target.dataset.moveIndex;
    if (idx === undefined) return;
    const moveIndex = Number(idx);
    if (Number.isNaN(moveIndex)) return;
    if (previewIndex === moveIndex) {
      exitPreview();
    } else {
      enterPreview(moveIndex);
    }
  });
}
humanSideEl.addEventListener('change', () => {
  exitPreview();
  updateOrientation();
  syncEngine();
  updateTopStatus();
});
applyEditBtn.addEventListener('click', () => {
  exitPreview();
  sideToMove = sideToMoveEl.value;
  castling.K = castleKEl.checked;
  castling.Q = castleQEl.checked;
  castling.k = castlekEl.checked;
  castling.q = castleqEl.checked;
  epSquare = '-';
  halfmove = 0;
  fullmove = 1;
  history = [];
  moveList = [];
  historyIndex = 0;
  lastMove = null;
  positionHistory.clear();
  recordPosition();
  pushHistory();
  sendUci('ucinewgame'); // Reset engine state after editing
  syncEngine();
  renderBoard();
  updateTopStatus();
});

document.addEventListener('DOMContentLoaded', () => {
  setupPalette();
  setStartPosition();
  connect();
});
