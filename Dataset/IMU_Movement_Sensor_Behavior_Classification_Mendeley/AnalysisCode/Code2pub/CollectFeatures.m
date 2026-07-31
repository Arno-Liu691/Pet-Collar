function [Xd, Class, Fnames, DogID, TestNum] = CollectFeatures(GFeat, SensNames)

% Collect data starting at the beginning of the first task
% common behaviors

LastVar = 12;
FirstBeh = 13;
NS = numel(SensNames);
for iS = 1:NS
    SeNa = SensNames{iS};
    Vnames = GFeat.(SeNa){1}.Properties.VariableNames;
    Vn1 = Vnames(1:LastVar);
    ab = Vnames(FirstBeh:end);
    for iDog = 2:height(GFeat)
        ab = intersect(ab,GFeat.(SeNa){iDog}.Properties.VariableNames(FirstBeh:end));
    end
    varList = [Vn1 ab];
    
    for iDog = 1:height(GFeat)
        sti = find(~isundefined(GFeat.(SeNa){iDog}.Task),1,'first');
        Dog = repmat(GFeat.DogID(iDog),height(GFeat.(SeNa){iDog})-sti+1,1);
        TestNum = repmat(GFeat.TestNum(iDog),height(GFeat.(SeNa){iDog})-sti+1,1);
        TD{iDog} = [table(Dog) table(TestNum) GFeat.(SeNa){iDog}(sti:end,varList)];
        % Add Galloping manually
        if ismember('Galloping',GFeat.(SeNa){iDog}.Properties.VariableNames)
            TD{iDog}.Galloping = GFeat.(SeNa){iDog}{sti:end,{'Galloping'}};
        else
            TD{iDog}.Galloping = zeros(height(TD{iDog}),1);
        end
    end
    fD.(SeNa) = cat(1,TD{:});
end

%% Assign labels
D = fD.Back; % Same in all sensors
% Collect part of behaviours, some overlap:  carrying... playing...
B2class = {'Walking' 'Standing' 'Lying_chest' 'Trotting' 'Sitting' 'Galloping' 'Sniffing' };
allBnam = strrep(B2class,'_',' ');
ClassData = D{:,B2class};

Bind = ClassData >= 75;
numM = sum(Bind,2);
[maxP, nB] = max(ClassData,[],2);
fD.Labels = cell(height(D),1);
fD.NoneInd = numM == 0;
fD.Labels(fD.NoneInd) = {'None'};
fD.OneInd = numM == 1;
fD.Labels(fD.OneInd) = allBnam(nB(fD.OneInd));
fD.MultiInd = numM == 2;
fD.Labels(fD.MultiInd) = {'Many'};
% Behs = {'Walking' 'Standing' 'Lying chest' 'Trotting' 'Sitting' 'Galloping' 'Sniffing' };

plInd = fD.OneInd;
Class = categorical(fD.Labels(plInd));
DogID = fD.Back.Dog(plInd); % Same in all sensors
TestNum = fD.Back.TestNum(plInd); % Same in all sensors
%%
for iS = 1:NS
    SeNa = SensNames{iS};
    tmp = [fD.(SeNa).AstdSum(plInd),...
        fD.(SeNa).AMeanV(plInd,:),...
        fD.(SeNa).AOff(plInd),...
        mean(fD.(SeNa).APeakNum(plInd,:),2),...
        fD.(SeNa).AecdfXYZ(plInd,:), ...
        fD.(SeNa).GstdSum(plInd),...
        fD.(SeNa).GMeanV(plInd,:),...
        fD.(SeNa).GOff(plInd),...
        mean(fD.(SeNa).GPeakNum(plInd,:),2),...
        fD.(SeNa).GecdfXYZ(plInd,:)];
    Xd{iS} = tmp;
end
FnA = {'ATotAct' 'AMeanX' 'AMeanY' 'AMeanZ' 'AOffset' 'ANMeanCros'};
FnG = {'GTotAct' 'GMeanX' 'GMeanY' 'GMeanZ' 'GOffset' 'GNMeanCros'};
xyz = 'XYZ';
for idir = 1:NS
    for ii = 1:7
        EcA{idir}{ii} = sprintf('A%s%d',xyz(idir),ii);
        EcG{idir}{ii} = sprintf('G%s%d',xyz(idir),ii);
    end
end
Fnames = cat(2,FnA,EcA{:},FnG,EcG{:});


