function criterion = Tree_ClassCrit_Neck(XTRAIN,ytrain,XTEST,ytest)


MaxSplits = 121; % esioptimoinnista rautalankana
tree = fitctree(XTRAIN,ytrain, ...
    'MaxNumSplits',MaxSplits,'CrossVal','off');

Yh =  predict(tree,XTEST);
criterion = sum(Yh~=ytest);
