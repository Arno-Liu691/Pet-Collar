function Res = ClassifyDogs_Tree(Xd, SensNames, Class, reliefVars, cvp, RrFT, DogID, treecvOp)

NS = numel(SensNames);
MaxSplits(1) = treecvOp{1}.HyperparameterOptimizationResults.XAtMinEstimatedObjective.MaxNumSplits; % esioptimoinnista rautalankana
MaxSplits(2) = treecvOp{2}.HyperparameterOptimizationResults.XAtMinEstimatedObjective.MaxNumSplits; % esioptimoinnista rautalankana

for iS = 1:NS
    SeNa = SensNames{iS};
    dataset = zscore(Xd{iS}(:,reliefVars{iS}(RrFT.fsV.Tree{iS})));
    tree = fitctree(dataset,Class, ...
        'MaxNumSplits',MaxSplits(iS),'cv',cvp);
    
    [label,score] = kfoldPredict(tree);
    
    [confMat, ord1] = confusionmat(Class,label);
    CorrRate = trace(confMat)/sum(confMat(:));
    
    Correct = (Class==label);
    [sums,nums] = grpsum(Correct,DogID);
    DogRate = sums./nums;
    
    Res.label.Tree{iS} = label;
    Res.confMat.Tree{iS} = confMat;
    Res.CorrRate.Tree{iS} = CorrRate;
    Res.DogRate.Tree{iS} = DogRate;
end


