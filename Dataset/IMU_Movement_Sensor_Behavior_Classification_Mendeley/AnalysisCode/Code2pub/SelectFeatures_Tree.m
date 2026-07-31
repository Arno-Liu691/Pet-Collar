function RrFT = SelectFeatures_Tree(Xd, SensNames, Class, reliefVars, cvp, treecvOp)

NS = numel(SensNames);
opts = statset('display','off');

Funs = {@Tree_ClassCrit_Back @Tree_ClassCrit_Neck};

MaxSplits(1) = treecvOp{1}.HyperparameterOptimizationResults.XAtMinEstimatedObjective.MaxNumSplits; 
MaxSplits(2) = treecvOp{2}.HyperparameterOptimizationResults.XAtMinEstimatedObjective.MaxNumSplits; 
%%
for iS = 1:NS
    SeNa = SensNames{iS};
    dataset = zscore(Xd{iS}(:,reliefVars{iS}));
    
    [fsV,history] = sequentialfs(Funs{iS}, ...
        dataset,Class,'cv',cvp,'options',opts);
    
    tree = fitctree(dataset,Class, ...
        'MaxNumSplits',MaxSplits(iS),'cv',cvp);
    
    [label,score] = kfoldPredict(tree);
    [confMat, ord1] = confusionmat(Class,label);
    CorrRate = trace(confMat)/sum(confMat(:));
    
    RrFT.label.Tree{iS} = label;
    RrFT.confMat.Tree{iS} = confMat;
    RrFT.CorrRate.Tree{iS} = CorrRate;
    RrFT.fsV.Tree{iS} = fsV;
end

