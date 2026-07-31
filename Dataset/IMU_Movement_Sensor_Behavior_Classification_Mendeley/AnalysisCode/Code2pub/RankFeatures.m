function [ranks, weights, k, kRanks, kweights] = RankFeatures(Xd, SensNames, Class, knums)

NS = numel(SensNames);

for k = knums
    for iS = 1:NS
        %     iS = 1;
        SeNa = SensNames{iS};
        dataset = zscore(Xd{iS});
        [ranks.(SeNa)(k,:),weights.(SeNa)(k,:)] = relieff(dataset,Class,k);
    end
end

%%
for iS = 1:NS
    SeNa = SensNames{iS};
    kRanks.(SeNa) = ranks.(SeNa)(k,:);
    kweights.(SeNa) = weights.(SeNa)(k,:);
end
