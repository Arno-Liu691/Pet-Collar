function Feat = CalcFeatures_Acc_Gyro(AccD, WLen, Overlap, Necdf)

if nargin < 4
    Necdf = 7; % default
end
if nargin < 3
    Overlap = 50; % Overlap 
end
if nargin < 2
    WLen = 200; % 2 sec window, assuming 100Hz 
end

fs = 100; % Should be 100Hz in these measurements
W=ones(WLen,1)/WLen;
if Overlap > 0
    WinIndex = WLen * (Overlap/fs);
else
    WinIndex = WLen;
end
fLimit = 0.2; 
minRlen = 5; 
WinSec = WLen/100; % assuming 100Hz 

VarNames = {'VarBack' 'VarNeck'};
SensNames = {'Back' 'Neck'};
Nsensor = numel(SensNames);
Feat = AccD(:,{'DogID' 'TestNum'});

%% Subtract "normal position" == "robust mean of standing"

AccNames = {'ABack' 'ANeck'};
for iDog = 1:height(AccD)
    ind = AccD.sensorData{iDog}.Task == 'Task stand' & ...
        any(AccD.sensorData{iDog}.Behavior == 'Standing',2);
    for iM = 1:Nsensor
        sens = AccNames{iM};
        AccD.sensorData{iDog}.(sens) = AccD.sensorData{iDog}.(sens) - ...
            trimmean(AccD.sensorData{iDog}.(sens)(ind,:),10);
    end
end


%% Features in WLen windows
AG = {'A' 'G'}; % Accelerometer, Gyro
for iSens = 1:Nsensor
    Sname = SensNames{iSens};
    Feat.(Sname) = cell(height(Feat),1);
    for iDog = 1:height(AccD)
        Nmax = floor(height(AccD.sensorData{iDog}) / WinIndex);
        NmaxF = Nmax-1;
        
        % Time not necessary
%         Vtime = AccD.sensorData{iDog}.Vt(1:WinIndex:WinIndex*Nmax);
%         Vtime = Vtime(1:NmaxF);

        % Arrange Tasks and Behaviors
        reTask = reshape(AccD.sensorData{iDog}.Task(1:WinIndex*Nmax),WinIndex,Nmax);
        Taskinds = all(~isundefined(reTask));
        Task = categorical(NaN(Nmax,1));
        Task(Taskinds) = reTask(1,Taskinds);
        Task = Task(1:end-1);
        % Behaviors
        Nb = size(AccD.sensorData{iDog}.Behavior,2);
        BehCats = categories(AccD.sensorData{iDog}.Behavior);
        Bpros = zeros(NmaxF,numel(BehCats));
        for iB = 1:Nb
            treB = reshape(AccD.sensorData{iDog}.Behavior(1:WinIndex*Nmax,iB),WinIndex,Nmax);
            reB{iB} = [treB(:,1:end-1);treB(:,2:end)];
            for itstep = 1:NmaxF
                Btab = tabulate(reB{iB}(:,itstep));
                if ~isequal(Btab(:,1),BehCats)
                    error('Categories...')
                end
                Bpros(itstep,:) = Bpros(itstep,:) + [Btab{:,2}];
            end
        end
        BprosMatrix = Bpros ./ WLen * 100;
        ValidCats = strrep(BehCats,' ','_');
        ValidCats = strrep(ValidCats,'<','LT');
        Behavior = array2table(BprosMatrix,'VariableNames',ValidCats);
        
%         T = table(Vtime, Task);
        T = table(Task);
        
        for iAG = 1:2
            AccName = [AG{iAG} Sname];
            
            % Mean and offset from zero
            F_Acc = filter(W,1,AccD.sensorData{iDog}.(AccName));
            MeanV = F_Acc(WinIndex:WinIndex:end,:);
            Off = sqrt(sum(MeanV.^2,2));
            
            % Total activity: Moving std
            MovVar = apply2matrix(@movingvar,AccD.sensorData{iDog}.(AccName),WLen);
            F_Std = sum(filter(W,1,sqrt(MovVar)),2);
            stdSum = F_Std(WinIndex:WinIndex:end);
            
            % ECDF features
            ecdfXYZ = zeros(NmaxF,3*Necdf);
            for itstep = 1:NmaxF
                ind1 = (itstep-1)*WinIndex + 1;
                ind2 = ind1 + WLen - 1;
                ll = zeros(Necdf,3);
                for xyz = 1:3
                    [f, x] = ecdf(AccD.sensorData{iDog}.(AccName)(ind1:ind2,xyz));
                    ll(:,xyz)=interp1(f,x,linspace(0,1,Necdf),'PCHIP');
                end
                ecdfXYZ(itstep,:) = ll(:);
            end
            
            % "Frequency" features
            PeakNum = zeros(NmaxF,3);
            for itstep = 1:NmaxF
                ind1 = (itstep-1)*WinIndex + 1;
                ind2 = ind1 + WLen - 1;
                for xyz = 1:3
                    xWin = AccD.sensorData{iDog}.(AccName)(ind1:ind2,xyz);
                    [rlg,rnum] = RunLength(xWin > MeanV(itstep,xyz) + fLimit);
                    PeakNum(itstep,xyz) = numel(rnum(rlg) >= minRlen) / WinSec;
                end
            end
            T.([AG{iAG} 'MeanV']) = MeanV(1:NmaxF,:);
            T.([AG{iAG} 'Off']) = Off(1:NmaxF);
            T.([AG{iAG} 'stdSum']) = stdSum(1:NmaxF);
            T.([AG{iAG} 'ecdfXYZ']) = ecdfXYZ;
            T.([AG{iAG} 'PeakNum']) = PeakNum;
        end
        
        Feat.(Sname){iDog} = [T Behavior];
    end
end

