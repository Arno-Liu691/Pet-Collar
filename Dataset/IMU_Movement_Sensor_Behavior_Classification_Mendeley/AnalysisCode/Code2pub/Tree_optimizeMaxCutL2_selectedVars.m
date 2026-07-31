function treecvOp = Tree_optimizeMaxCutL2_selectedVars(Xd, SensNames, Class, cvp, reliefVars, RrFT)


for iS = 1:2 % Vain Back & Neck
    SeNa = SensNames{iS};
    Vin = reliefVars{iS}(RrFT.fsV.Tree{iS});
    dataset = zscore(Xd{iS}(:,Vin));
%    opops.UseParallel = true; % Use only if parallel toolbos is available
    opops.CVPartition = cvp;
    opops.Verbose = 0;
    more off
    treecvOp{iS} = fitctree(dataset,Class, ...
        'OptimizeHyperparameters',{'MaxNumSplits'}, ...
        'HyperparameterOptimizationOptions',opops);%,'CVPartition',cmD);
    % treecv5 = fitctree(dataset,Class,'MaxNumSplits',50,'KFold',5);
end


