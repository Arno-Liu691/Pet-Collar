function criterion = Tree_ClassCrit_Back(XTRAIN,ytrain,XTEST,ytest)


MaxSplits = 124; % esioptimoinnista rautalankana
tree = fitctree(XTRAIN,ytrain, ...
    'MaxNumSplits',MaxSplits,'CrossVal','off');

Yh =  predict(tree,XTEST);
criterion = sum(Yh~=ytest);
