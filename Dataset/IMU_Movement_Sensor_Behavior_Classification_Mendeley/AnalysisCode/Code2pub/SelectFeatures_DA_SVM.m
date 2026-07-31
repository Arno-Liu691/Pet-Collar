function RrF = SelectFeatures_DA_SVM(Xd, SensNames, Class, reliefVars, cvp)

NS = numel(SensNames);
DAtype = {'linear' 'quadratic'}; % linear and quadratic discriminants

opts = statset('display','off');
MNUM = more('off');
for iS = 1:NS 
    SeNa = SensNames{iS};
    dataset = zscore(Xd{iS}(:,reliefVars{iS}));
    
    % LDA & QDA
    for iDA = 1:2
        
        DAt = DAtype{iDA};
        fun = @(XT,yT,Xt,yt)(sum(yt~=classify(Xt,XT,yT,DAt)));
        
        [fsV,history] = sequentialfs(fun,dataset,Class,'cv',cvp,'options',opts);
        
        obj = fitcdiscr(dataset(:,fsV),Class,'cv' ,cvp,'DiscrimType',DAt); %LDA 'quadratic' linear
        [label,score] = kfoldPredict(obj);
        [confMat, ord1] = confusionmat(Class,label);
        CorrRate = trace(confMat)/sum(confMat(:));
        
        RrF.label.(DAt){iS} = label;
        RrF.confMat.(DAt){iS} = confMat;
        RrF.CorrRate.(DAt){iS} = CorrRate;
        RrF.fsV.(DAt){iS} = fsV;
        
    end

    % SVM
    [fsV,history] = sequentialfs(@SVM_ClassCrit,dataset,Class, ...
        'cv',cvp,'options',opts);
    t = templateSVM('KernelFunction','gaussian');
    Mdl = fitcecoc(dataset(:,fsV),Class,'Learners',t, ...
        'CVPartition',cvp);
    
    label =  kfoldPredict(Mdl);
    [confMat, ord_L] = confusionmat(Class,label);
    CorrRate = trace(confMat)/sum(confMat(:));
    RrF.label.SVM{iS} = label;
    RrF.confMat.SVM{iS} = confMat;
    RrF.CorrRate.SVM{iS} = CorrRate;
    RrF.fsV.SVM{iS} = fsV;
end
more(MNUM)


