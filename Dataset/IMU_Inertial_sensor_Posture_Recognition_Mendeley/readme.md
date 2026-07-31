df_raw.csv file columns:

- Demographics: relevant information for grouping
    - Subject: dog code.
    - Breed: dog breed. LR (= Labrador Retriever). GR (=  Golden Retriever. LRxGR (= Labrador crossed with Golden Retriever).

- Inertial data: Combination of the following: 3 IMUs (back, chest, neck), 3 sensors (Accelerometer, Gyroscrop, Magnetormeter), 3 axis (x,y,z) = 18 columns.

- Targets: 
    - 'Type' = 'static' or 'dynamic' 
    - 'Posture' = 'standing', 'walking', 'sitting', 'lying down', 'body shake'

df_dogs.csv file columns:

- Subject: dog code.
- Breed: dog breed. LR (= Labrador Retriever). GR (=  Golden Retriever. LRxGR (= Labrador crossed with Golden Retriever).
- Sex: Female (F), Male (M).
- DOB: Date of Birth.