%% Scenario1

cd './Scenario1_FaintedDogs';

MyFolderListInfo = dir('./No*');

for ml_i = 13:length(MyFolderListInfo) 
    
    File_Name = MyFolderListInfo(ml_i).name;
    
    cd (['./' File_Name])
    
    cd ('./Radar')
    
    MyFolderListInfo2 = dir('./RawData_No*');
    
    name = MyFolderListInfo2(1).name;
    
    a = load(MyFolderListInfo2(1).name);
    
    MyMatrix=cell2mat(struct2cell(a));
    
    cd ('../../')
end

%% Scenario2

cd './Scenario2_AwakeDogs';

MyFolderListInfo = dir('./No*');

for ml_i = 13:length(MyFolderListInfo) 
    
    File_Name = MyFolderListInfo(ml_i).name;
    
    cd (['./' File_Name])
    
    cd ('./Radar')
    
    MyFolderListInfo2 = dir('./RawData_No*');
    
    name = MyFolderListInfo2(1).name;
    
    a = load(MyFolderListInfo2(1).name);
    
    MyMatrix=cell2mat(struct2cell(a));
    
    cd ('../../')
end
