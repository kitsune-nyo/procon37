const $ = id => document.getElementById(id);
const colors = ['#6ce3bb','#f5be6c','#86aaff','#ed91c8','#c4a3ff','#70cfed','#f08f7e','#d5de80'];
const terrainNames = ['平地','道路','山','池'];
const terrainColors = ['#263d36','#566274','#66543a','#193e61'];
const ns = 'http://www.w3.org/2000/svg';
let data = null, dayIndex = 0, tick = 0, selected = null, timer = null, loadVersion = 0;
function el(tag, attrs = {}, text = '') { const e = document.createElement(tag); for (const [k,v] of Object.entries(attrs)) e.setAttribute(k,v); e.textContent = text; return e; }
function svg(tag, attrs, text = '') {const e = document.createElementNS(ns,tag); for(const [k,v] of Object.entries(attrs))e.setAttribute(k,v);e.textContent=text;return e;}
function currentDay(){return data?.days[dayIndex];}
function stop(){clearInterval(timer);timer=null;$('play').textContent='▶ 再生';}
function seek(t){stop();tick=Math.max(0,Math.min(currentDay()?.steps||0,t));render();}
function point(pos){const {width}=data.setting.map;const r=Math.floor(pos/width),c=pos%width;return [26+c*34.641+(r%2===0?17.321:0),25+r*30];}
function renderMap(){
  const root=$('map');root.replaceChildren();if(!data)return;
  const {width,height,cells}=data.setting.map,day=currentDay(),frame=day?.frames?.[tick];
  root.setAttribute('viewBox',`0 0 ${width*34.641+54} ${height*30+30}`);
  const traffics=new Map((day?.info.traffics||[]).map(t=>[t.pos,t.status]));
  const spots=new Map(data.setting.spots.map(s=>[s.pos,s]));
  for(let r=0;r<height;r++)for(let c=0;c<width;c++){
    const pos=r*width+c,[x,y]=point(pos),t=cells[r][c];
    const points=Array.from({length:6},(_,i)=>{const a=(30+i*60)*Math.PI/180;return `${x+20*Math.cos(a)},${y+20*Math.sin(a)}`;}).join(' ');
    const group=svg('g',{}),poly=svg('polygon',{points,fill:terrainColors[t],stroke:traffics.get(pos)?'#ed945b':'#172530','stroke-width':traffics.get(pos)?2:.8});
    group.append(poly);const s=spots.get(pos);
    if(s){group.append(svg('path',{d:`M ${x} ${y-8} l 7 8 l -7 8 l -7 -8 Z`,fill:'#f2d889',opacity:.85}));}
    if($('positions').checked)group.append(svg('text',{x,y:y+4,'text-anchor':'middle',fill:'#d4dfeb','font-size':9},String(pos)));
    group.addEventListener('mouseenter',()=>{$('map-tip').textContent=`地点 ${pos} / 行 ${r}, 列 ${c} / ${terrainNames[t]}${traffics.has(pos)?` / 道路状況: ${['円滑','混雑','渋滞'][traffics.get(pos)]}`:''}${s?` / ブランド ${s.brand} / 残り在庫 ${frame?.stocks[pos]??s.stocks}`:''}`;});
    root.append(group);
  }
  if(day?.frames&&$('routes').checked){
    day.info.agents.forEach((a,i)=>{
      if(selected!==null&&selected!==i)return;
      let from=a.pos;
      for(const e of day.events.filter(e=>e.agent===i&&e.type==='move')){
        const p=point(from),q=point(e.dest);root.append(svg('line',{x1:p[0],y1:p[1],x2:q[0],y2:q[1],stroke:colors[i%8],'stroke-width':2.5,opacity:e.until<=tick ? .8 : .35,'stroke-dasharray':e.until<=tick?'':'4 3','pointer-events':'none'}));from=e.dest;
      }
    });
  }
  const agents=frame?.agents||day?.info.agents||data.setting.agents.map(pos=>({pos,kind:null}));
  const counts=new Map();
  agents.forEach((a,i)=>{
    let [x,y]=point(a.pos);if(a.dest!==undefined){const q=point(a.dest),p=(tick-a.start)/(a.until-a.start);x+=(q[0]-x)*p;y+=(q[1]-y)*p;}
    const key=`${x},${y}`,overlap=counts.get(key)||0;counts.set(key,overlap+1);x+=overlap*8;y-=overlap*8;
    const group=svg('g',{tabindex:0,role:'button','aria-label':`車両 ${i+1} を選択`,style:'cursor:pointer',opacity:selected!==null&&selected!==i ? .45 : 1});
    group.append(a.kind===1?svg('rect',{x:x-10,y:y-10,width:20,height:20,rx:4,fill:colors[i%8],stroke:'#fff','stroke-width':selected===i?2:1}):svg('circle',{cx:x,cy:y,r:10,fill:colors[i%8],stroke:'#fff','stroke-width':selected===i?2:1}));
    group.append(svg('text',{x,y:y+4,'text-anchor':'middle',fill:'#0c141d','font-size':11,'font-weight':700},String(i+1)));
    group.append(svg('title',{},`車両 ${i+1} / ${a.kind===1?'補給車':a.kind===0?'巡回車':'役割未記録'} / 地点 ${a.pos}`));
    const choose=()=>{selected=selected===i?null:i;render();};group.addEventListener('click',choose);group.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();choose();}});root.append(group);
  });
}
function render(){
  if(!data)return;const day=currentDay(),frame=day?.frames?.[tick],steps=day?.steps||0;
  $('step').max=steps;$('step').value=tick;$('time').textContent=`STEP ${tick} / ${steps}`;
  for(const id of ['step','prev','next','play'])$(id).disabled=!frame;
  const accepted=day?.submission?.accepted;
  $('accept').textContent=!day?'盤面設定のみ':day.pending?'計画待ち':accepted===true?'送信受理済み':accepted===false?'送信失敗 / 未受理':'受理結果未記録';
  $('accept').className=`pill ${accepted===false?'bad':''}`;
  const v=day?.verification;
  $('verification').className=`note ${v?v.matches?'good':'bad':''}`;
  $('verification').textContent=v?(v.matches?'✓ 翌日の観測と日末の位置・燃料が一致しました。':'⚠ 翌日の観測と予測が異なります。各車両の日末状態を確認してください。'):frame?'予測リプレイ / 翌日の観測による確認はまだありません。':'このファイルには行動履歴がありません。履歴付きの試合ログを選択してください。';
  $('selected').textContent=selected===null?'全車両を表示':`車両 ${selected+1} の履歴`;
  $('agents').replaceChildren();
  const agents=frame?.agents||day?.info.agents||data.setting.agents.map(pos=>({pos,kind:null}));
  agents.forEach((a,i)=>{
    const card=el('button',{class:`agent ${selected===i?'active':''}`,style:`--car:${colors[i%8]}`});
    card.append(el('strong',{},`${a.kind===1?'□ 補給車':a.kind===0?'○ 巡回車':'車両'} ${i+1}`));
    card.append(el('span',{class:'state'},`地点 ${a.pos}${a.dest!==undefined?` → ${a.dest} (${a.until-tick} step 後に到着)`:''} / ${a.status||'開始位置'}`));
    card.append(el('div',{},a.kind===1?'燃料消費なし':a.fuel===undefined?'役割・燃料未記録':`燃料 ${a.fuel} / ${data.setting.fuelLimits}`));
    if(a.kind===0){const bar=el('div',{class:'fuel'});bar.append(el('b',{style:`width:${Math.min(100,a.fuel/data.setting.fuelLimits*100)}%`}));card.append(bar);}
    const st=day?.stats?.[i];if(st)card.append(el('div',{class:'metrics'},`1日合計: 移動 ${st.moves} 回 (${st.moving} step) / 待機 ${st.wait} step / ${a.kind===1?`補給 ${st.supplies} 回`:`補給受信 ${st.received} 回 / 取得 ${st.udon} 杯`}`));
    if(v&&!v.matches){card.append(el('div',{class:'metrics bad'},`日末予測 ${v.predicted[i]?.pos} / 燃料 ${v.predicted[i]?.fuel} ↔ 観測 ${v.actual[i]?.pos} / 燃料 ${v.actual[i]?.fuel}`));}
    card.addEventListener('click',()=>{selected=selected===i?null:i;render();});$('agents').append(card);
  });
  $('plans').textContent=day?.plans?day.plans.map((p,i)=>`車両 ${i+1}: ${JSON.stringify(p)}`).join('\n')+'\n\n0=北西 1=北東 2=東 3=南東 4=南西 5=西 / 負数=その絶対値のステップ数だけ待機':'行動列は未記録です。';
  const list=$('events'),scroll=list.scrollTop;list.replaceChildren();
  const filter=$('event-type').value,types={supply:['supply','receive','meet'],move:['move','arrive'],wait:['wait'],collect:['collect']};
  const events=(day?.events||[]).filter(e=>(selected===null||e.agent===selected)&&(!filter||types[filter].includes(e.type))&&($('future').checked||e.step<=tick));
  for(const e of events){const row=el('button',{class:`event ${e.type} ${e.step===tick?'current':''} ${e.step>tick?'future':''}`});const head=el('div',{class:'event-head'});head.append(el('span',{},`STEP ${String(e.step).padStart(3,'0')}`),el('span',{style:`color:${colors[e.agent%8]}`},`車両 ${e.agent+1}`));row.append(head,el('div',{},e.text));row.addEventListener('click',()=>seek(e.step));list.append(row);}
  if(!events.length)list.append(el('div',{class:'empty'},frame?'この条件に該当する行動はありません。':'試合実行後に output/replays の記録を選ぶと、1日ごとの行動が表示されます。'));
  list.scrollTop=scroll;renderMap();
}
function setData(result){stop();data=result;tick=0;dayIndex=0;selected=null;$('day').replaceChildren();data.days.forEach((d,i)=>$('day').append(el('option',{value:i},`${d.day+1} 日目 (API day=${d.day})`)));$('day').disabled=!data.days.length;if(!data.days.length)$('day').append(el('option',{},'履歴なし'));render();}
async function jsonResponse(response){const result=await response.json();if(!response.ok)throw Error(result.error||'読み込みに失敗しました');return result;}
async function loadFile(){const version=++loadVersion;try{$('message').textContent='';const result=await jsonResponse(await fetch('/api/replay?file='+encodeURIComponent($('file').value)));if(version===loadVersion)setData(result);}catch(e){if(version===loadVersion)$('message').textContent=e.message;}}
async function refresh(){try{const old=$('file').value;const files=await jsonResponse(await fetch('/api/files'));$('file').replaceChildren();files.forEach(f=>$('file').append(el('option',{value:f.path},`${f.history?'履歴':'盤面'} / ${f.name}`)));if(files.some(f=>f.path===old))$('file').value=old;else if(!files.some(f=>f.history)&&files.some(f=>f.name==='testes.json'))$('file').value=files.find(f=>f.name==='testes.json').path;if(files.length)await loadFile();else $('message').textContent='記録がありません。JSON / JSONL ファイルを開いてください。';}catch(e){$('message').textContent=e.message;}}
$('file').addEventListener('change',loadFile);$('refresh').addEventListener('click',refresh);
$('upload').addEventListener('change',async e=>{const file=e.target.files[0];if(!file)return;const version=++loadVersion;try{if(file.size>20000000)throw Error('ファイルの上限は 20 MB です');const result=await jsonResponse(await fetch('/api/import',{method:'POST',body:await file.text()}));if(version===loadVersion){setData(result);$('message').textContent=`読み込み: ${file.name}`;}}catch(error){$('message').textContent=error.message;}e.target.value='';});
$('day').addEventListener('change',()=>{stop();dayIndex=Number($('day').value);tick=0;render();});
$('step').addEventListener('input',()=>seek(Number($('step').value)));$('prev').addEventListener('click',()=>seek(tick-1));$('next').addEventListener('click',()=>seek(tick+1));
$('play').addEventListener('click',()=>{if(timer){stop();return;}if(!currentDay()?.frames)return;if(tick===currentDay().steps)tick=0;$('play').textContent='Ⅱ 停止';timer=setInterval(()=>{tick++;render();if(tick>=currentDay().steps)stop();},Number($('speed').value));render();});
$('speed').addEventListener('change',()=>{if(timer){stop();$('play').click();}});
for(const id of ['routes','positions','event-type','future'])$(id).addEventListener('change',render);
$('all').addEventListener('click',()=>{selected=null;render();});
document.addEventListener('keydown',e=>{if(['INPUT','SELECT','TEXTAREA','BUTTON'].includes(e.target.tagName))return;if(e.code==='Space'){e.preventDefault();$('play').click();}else if(e.key==='ArrowRight'){e.preventDefault();seek(tick+1);}else if(e.key==='ArrowLeft'){e.preventDefault();seek(tick-1);}});
refresh();
