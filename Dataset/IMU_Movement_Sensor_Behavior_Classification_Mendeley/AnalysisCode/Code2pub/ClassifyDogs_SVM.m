function Res = ClassifyDogs_SVM(Xd, SensNames, Class, reliefVars, cvp, RrF, DogID)

NS = numel(SensNames);

for iS = 1:NS
    SeNa = SensNames{iS};
    dataset = zscore(Xd{iS}(:,reliefVars{iS}(RrF.fsV.SVM{iS})));
    t = templateSVM('KernelFunction','gaussian');
    Mdl = fitcecoc(dataset,Class,'Learners',t, ...
        'CVPartition',cvp);
    
    label =  kfoldPredict(Mdl);
    [confMat, ord1] = confusionmat(Class,label);
    CorrRate = trace(confMat)/sum(confMat(:));
    
    Correct = (Class==label);
    [sums,nums] = grpsum(Correct,DogID);
    DogRate = sums./nums;
    
    Res.label.SVM{iS} = label;
    Res.confMat.SVM{iS} = confMat;
    Res.CorrRate.SVM{iS} = CorrRate;
    Res.DogRate.SVM{iS} = DogRate;
end


