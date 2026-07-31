function Res = ClassifyDogs_DA(Xd, SensNames, Class, reliefVars, cvp, RrF, DogID)

NS = numel(SensNames);
DAtype = {'linear' 'quadratic'}; % linear and quadratic discriminants

opts = statset('display','off');

for iS = 1:NS
    SeNa = SensNames{iS};
    % LDA & QDA
    for iDA = 1:2
        DAt = DAtype{iDA};
        dataset = zscore(Xd{iS}(:,reliefVars{iS}(RrF.fsV.(DAt){iS})));
        obj = fitcdiscr(dataset,Class,'cv' ,cvp,'DiscrimType',DAt); %LDA 'quadratic' linear
        [label,score] = kfoldPredict(obj);
        [confMat, ord1] = confusionmat(Class,label);
        CorrRate = trace(confMat)/sum(confMat(:));
        
        Correct = (Class==label);
        [sums,nums] = grpsum(Correct,DogID);
        DogRate = sums./nums;
        
        Res.label.(DAt){iS} = label;
        Res.confMat.(DAt){iS} = confMat;
        Res.CorrRate.(DAt){iS} = CorrRate;
        Res.DogRate.(DAt){iS} = DogRate;
        
    end
end


