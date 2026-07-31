function criterion = SVM_ClassCrit(XTRAIN,ytrain,XTEST,ytest)
options = statset('UseParallel',true);
t = templateSVM('KernelFunction','gaussian'); % 'Standardize',1,
Mdl = fitcecoc(XTRAIN,ytrain,'Learners',t,'Options',options);
% CVMdl = crossval(Mdl);
% CvLoss = kfoldLoss(CVMdl);
% Yh =  kfoldPredict(CVMdl);
% [Cm, ord_L] = confusionmat(Class,ytest);

Yh =  predict(Mdl,XTEST);
criterion = sum(Yh~=ytest);


