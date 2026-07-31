function treecvOp = Tree_optimizeMaxCutL1(Xd, SensNames, Class, reliefVars, cvp)

NS = numel(SensNames);

for iS = 1:NS 
    SeNa = SensNames{iS};
    dataset = zscore(Xd{iS}(:,reliefVars{iS}));
    % treecv = fitctree(dataset,Class,'MaxNumSplits',25,'CVPartition',cmD);
%    opops.UseParallel = true; % Use only if parallel toolbos is available
    opops.CVPartition = cvp;
    opops.Verbose = 0;
    more off
    treecvOp{iS} = fitctree(dataset,Class, ...
        'OptimizeHyperparameters',{'MaxNumSplits'}, ...
        'HyperparameterOptimizationOptions',opops);
end

