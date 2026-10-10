// オフライン検証: ランダムな試合を作り、solveDay の計画を独立実装のジャッジで判定する。
// 使い方: offline_test <seed数> <1日の探索秒数> [日数上限]
#define main hexudon_entry
#include "hexudon_main.cpp"
#undef main
#include <random>

static const int DT[2][6][2] = {{{-1,0},{-1,1},{0,1},{1,1},{1,0},{0,-1}},{{-1,-1},{-1,0},{0,1},{1,0},{1,-1},{0,-1}}};

struct Judge {
    int W, H; std::vector<int> cells; std::vector<int> road; // road status per cell
    int nb(int p, int d) { int r=p/W,c=p%W; int nr=r+DT[r&1][d][0], nc=c+DT[r&1][d][1];
        if(nr<0||nr>=H||nc<0||nc>=W) return -1; return nr*W+nc; }
    std::pair<int,int> cost(int p) { int t=cells[p]; if(t==0) return {2,1}; if(t==2) return {3,2};
        if(t==1){ int s=road[p]; return {s==0?1:(s==1?2:4),2}; } return {0,0}; }
};

struct DayResult { bool valid; std::string err; std::set<int> brands; int udon; int idleSteps; int refuel=0; int clump=0; int lowFuelSteps=0; int stuckMax=0; int stuckCnt=0; int multiSupply=0; int endOnSpot=0; int step1Udon=0; int lateStuck=0; };

DayResult judgeDay(Judge& J, const std::vector<SpotConfig>& spots, std::vector<AgentState>& ag, int fuelLimit,
                   int N, const std::vector<std::vector<int>>& plans) {
    DayResult R{true,"",{},0,0};
    int n = ag.size();
    if ((int)plans.size()!=n) return {false,"plan count",{},0,0};
    std::vector<int> stock; std::map<int,int> spotIdx;
    for (int i=0;i<(int)spots.size();i++){ stock.push_back(spots[i].stocks); spotIdx[spots[i].pos]=i; }
    std::vector<std::set<int>> got(n);
    std::vector<size_t> ip(n,0); std::vector<int> busyUntil(n,0), dest(n,-1), fcost(n,0);
    for (int i=0;i<n;i++){ long s=0; for(int a:plans[i]){ if(a>=0&&a<=5) s+=J.cost(0).first*0; } }
    // 合計ステップ数の検証は実行しながら行う
    std::vector<int> waitLeft(n,0);
    for (int t=0;t<=N;t++){
        if (t>0){
            for(int i=0;i<n;i++) if(dest[i]!=-1 && busyUntil[i]==t){ if(ag[i].kind==0) ag[i].fuel-=fcost[i]; }
            for(int i=0;i<n;i++) if(dest[i]!=-1 && busyUntil[i]==t){ ag[i].pos=dest[i]; dest[i]=-1; }
            for(int i=0;i<n;i++){ if(ag[i].kind!=0) continue; auto it=spotIdx.find(ag[i].pos);
                if(it==spotIdx.end()) continue; int k=it->second; if(got[i].count(k)||stock[k]<=0) continue;
                stock[k]--; got[i].insert(k); R.udon++; R.brands.insert(spots[k].brand); if(t==1) R.step1Udon++; }
            for(int i=0;i<n;i++) for(int j=i+1;j<n;j++) if(ag[i].kind==1&&ag[j].kind==1&&ag[i].pos==ag[j].pos) R.clump++;
            for(int i=0;i<n;i++) if(ag[i].kind==0 && ag[i].fuel*4<fuelLimit) R.lowFuelSteps++;
            for(int i=0;i<n;i++){ if(ag[i].kind!=0) continue; for(int j=0;j<n;j++) if(ag[j].kind==1&&ag[j].pos==ag[i].pos){ R.refuel+=fuelLimit-ag[i].fuel; ag[i].fuel=fuelLimit; } }
        }
        if (t==N) break;
        // 診断
        { static std::vector<int> streak; if(t==0) streak.assign(n,0);
          for(int i=0;i<n;i++){ if(ag[i].kind!=0) continue;
            bool pairIdle=false; int sup=0;
            for(int j=0;j<n;j++) if(ag[j].kind==1&&ag[j].pos==ag[i].pos){ sup++; if(busyUntil[i]<=t && busyUntil[j]<=t && ip[i]<plans[i].size() && plans[i][ip[i]]<0 && ip[j]<plans[j].size() && plans[j][ip[j]]<0) pairIdle=true; }
            if(sup>=2) R.multiSupply++;
            if(pairIdle){ streak[i]++; if(streak[i]==10){ R.stuckCnt++;
                int cand=0; for(size_t k=0;k<spots.size();k++) if(stock[k]>0&&!got[i].count(k)&&spots[k].pos!=ag[i].pos) cand++;
                if(getenv("DIAG")) printf("    STUCK t=%d/%d agent=%d pos=%d fuel=%d/%d spotsLeftForHim=%d\n",t,N,i,ag[i].pos,ag[i].fuel,fuelLimit,cand); }
              R.stuckMax=std::max(R.stuckMax,streak[i]); }
            else streak[i]=0; } }
        for(int i=0;i<n;i++){
            if (busyUntil[i]>t) continue;
            if (ip[i]>=plans[i].size()) return {false,"plan too short agent "+std::to_string(i),{},0,0};
            int a=plans[i][ip[i]];
            if (a<0){ // 待機 |a| ステップ（分割して消費）
                if (waitLeft[i]==0) waitLeft[i]=-a;
                waitLeft[i]--; busyUntil[i]=t+1; if(ag[i].kind==0) R.idleSteps++;
                if(waitLeft[i]==0) ip[i]++;
                continue;
            }
            if (a>5) return {false,"bad dir",{},0,0};
            int np=J.nb(ag[i].pos,a); if(np<0||J.cells[np]==3) return {false,"bad move",{},0,0};
            auto c=J.cost(ag[i].pos);
            if (ag[i].kind==0 && ag[i].fuel<c.second) return {false,"no fuel",{},0,0};
            if (t+c.first>N) return {false,"overrun",{},0,0};
            busyUntil[i]=t+c.first; dest[i]=np; fcost[i]=c.second; ip[i]++;
        }
    }
    for(int i=0;i<n;i++) if(ag[i].kind==0 && spotIdx.count(ag[i].pos)) R.endOnSpot++;
    for(int i=0;i<n;i++) if(ip[i]!=plans[i].size()||waitLeft[i]!=0) return {false,"plan too long agent "+std::to_string(i),{},0,0};
    return R;
}

int main(int argc, char** argv) {
    int seeds = argc>1?atoi(argv[1]):5; double budget = argc>2?atof(argv[2]):1.0; int maxDays = argc>3?atoi(argv[3]):6; int forceSupply = argc>4?atoi(argv[4]):0; double fuelMul = argc>5?atof(argv[5]):0;
    long tClump=0, tLow=0, tEos=0, tS1=0; long totalBrands=0, totalDaily=0, totalUdon=0, invalid=0, idle=0;
    for (int seed=1; seed<=seeds; seed++){
        std::mt19937 rng(seed*7919);
        auto U=[&](int a,int b){ return std::uniform_int_distribution<int>(a,b)(rng); };
        MatchConfig cfg; int W=U(12,32), H=U(12,32);
        cfg.map.width=W; cfg.map.height=H; cfg.map.cells.assign(H,std::vector<int>(W,0));
        for(int r=0;r<H;r++) for(int c=0;c<W;c++){ int x=U(0,99); cfg.map.cells[r][c]= x<10?2:(x<15?3:0); }
        for(int k=0;k<U(2,5);k++){ int r=U(0,H-1); for(int c=0;c<W;c++) cfg.map.cells[r][c]=1; }
        for(int k=0;k<U(1,4);k++){ int c=U(0,W-1); for(int r=0;r<H;r++) cfg.map.cells[r][c]=1; }
        // 連結化
        Judge J; J.W=W; J.H=H; J.cells.resize(W*H); for(int i=0;i<W*H;i++) J.cells[i]=cfg.map.cells[i/W][i%W];
        int st=0; while(J.cells[st]==3) st++; std::vector<int> seen(W*H,0); std::vector<int> q{st}; seen[st]=1;
        for(size_t h=0;h<q.size();h++) for(int d=0;d<6;d++){ int p=J.nb(q[h],d); if(p>=0&&!seen[p]&&J.cells[p]!=3){seen[p]=1;q.push_back(p);} }
        for(int i=0;i<W*H;i++) if(!seen[i]){ J.cells[i]=3; cfg.map.cells[i/W][i%W]=3; }
        std::vector<int> plains; for(int i=0;i<W*H;i++) if(J.cells[i]==0) plains.push_back(i);
        std::shuffle(plains.begin(),plains.end(),rng);
        int nAg=forceSupply?U(std::max(3,forceSupply+2),8):U(3,8), B=U(2,6), S=U(B,3*B); size_t pi=0;
        for(int k=0;k<S;k++) cfg.spots.push_back({k<B?k:U(0,B-1), plains[pi++], U(1,nAg)});
        for(int k=0;k<nAg;k++) cfg.agents.push_back(plains[pi++]);
        int D=std::min(maxDays,U(4,10));
        for(int d=0;d<D;d++){ cfg.daySteps.push_back(U(W+H,4*(W+H))); cfg.daySeconds.push_back((int)std::ceil(budget/0.65)+2); }
        cfg.fuelLimits=(int)(cfg.daySteps[0]*(1.0+2.0*U(0,100)/100.0)); if(fuelMul>0) cfg.fuelLimits=(int)(cfg.daySteps[0]*fuelMul);
        cfg.players=4; cfg.busyThreshold=2; cfg.jammedThreshold=4;
        cfg.startsAt=(long long)std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()+(long long)std::ceil(budget/0.65)+1;

        std::vector<int> kinds;
        if (forceSupply) { kinds.assign(nAg,0); for(int k=0;k<forceSupply;k++) kinds[nAg-1-k]=1; }
        else kinds = agentsKind(cfg);
        std::vector<AgentState> ag; for(int k=0;k<nAg;k++) ag.push_back({kinds[k],cfg.agents[k],kinds[k]==0?cfg.fuelLimits:0});
        std::set<int> brandsAll, solverBrands; int daily=0, udon=0, bad=0, idl=0, ref=0, clump=0, low=0, stuckC=0, stuckM=0, multi=0, eos=0, s1=0;
        for(int d=0;d<D;d++){
            DayInfo info; info.day=d; info.endsAt=0; info.agents=ag;
            J.road.assign(W*H,0);
            if(d>0) for(int i=0;i<W*H;i++) if(J.cells[i]==1){ int x=U(0,9); int s=x<6?0:(x<9?1:2); J.road[i]=s; info.traffics.push_back({i,s}); }
            Map map = buildMap(cfg, info);
            std::set<int> acq;
            auto plans = solveDay(map, cfg, info, cfg.daySteps[d], budget, solverBrands, &acq);
            solverBrands.insert(acq.begin(),acq.end());
            auto R = judgeDay(J, cfg.spots, ag, cfg.fuelLimits, cfg.daySteps[d], plans);
            if(!R.valid){ bad++; std::cout<<"  INVALID seed "<<seed<<" day "<<d<<": "<<R.err<<"\n"; continue; }
            brandsAll.insert(R.brands.begin(),R.brands.end()); daily+=R.brands.size(); udon+=R.udon; idl+=R.idleSteps; ref+=R.refuel; stuckC+=R.stuckCnt; stuckM=std::max(stuckM,R.stuckMax); multi+=R.multiSupply; eos+=R.endOnSpot; s1+=R.step1Udon; clump+=R.clump; low+=R.lowFuelSteps;
        }
        int supplies=0; for(int k:kinds) supplies+=k;
        printf("RESULT seed=%d map=%dx%d agents=%d(supply %d) brands=%d/%d daily=%d udon=%d invalid=%d patrolIdle=%d refueled=%d supplyClump=%d patrolLowFuel=%d stuck10=%d stuckMax=%d multiSupply=%d endOnSpot=%d step1Udon=%d\n",
            seed,W,H,nAg,supplies,(int)brandsAll.size(),B,daily,udon,bad,idl,ref,clump,low,stuckC,stuckM,multi,eos,s1); tEos+=eos; tS1+=s1;
        tClump+=clump; tLow+=low;
        totalBrands+=brandsAll.size(); totalDaily+=daily; totalUdon+=udon; invalid+=bad; idle+=idl;
    }
    printf("TOTAL brands=%ld daily=%ld udon=%ld invalid=%ld patrolIdle=%ld supplyClump=%ld patrolLowFuel=%ld endOnSpot=%ld step1Udon=%ld\n",totalBrands,totalDaily,totalUdon,invalid,idle,tClump,tLow,tEos,tS1);
}
