function [Gsums, Gnums] = grpsum(x,gv)
% Gsums = grpsum(x,gv)

unig = unique(gv);
N = numel(unig);
Gsums = zeros(N,1);
Gnums = zeros(N,1);
for ii = 1:N
    Gi = gv==unig(ii);
    Gnums(ii) = sum(Gi);
    Gsums(ii) = sum(x(Gi));
end



