%-*-octave-*--
%
% timefunc: setup a linear combination of polynomial functions of time
%
% USAGE:
% 
% [pd, td] = timefunc(D, nsteps, verbose)
%
% INPUT:
% D:  number of time functions (Must be positive)
% nsteps: number of time steps (1,2,...,nsteps) (optional, default nsteps = 100)
%
% OUTPUT:
%
% td(1:nsteps): 1-D array of midpoint time values
% pad(1:nsteps, 1:D): 2-D array of polynomials evaluated at the midpoint time levels
%
function  [pad, td] = timefunc(D, nsteps, verbose)

  d1 = 24.64579437;
  
  if nargin < 1
    D=2;
  end

  if nargin < 2
    nsteps=100;
  end

  if nargin < 3
    verbose = 0;
  end

  if D>26*2
    printf("The number of parameters D= %d exceeds 52 (not currently implemented)\n");
    return;
  end
  
# Final time T
  T = 15;
  dt = T/(nsteps+1);;
  td = linspace(0.5*dt, T-0.5*dt, nsteps)'; # column vector
  if (verbose==1)
    printf("Final time = %e, number of time steps = %d, time step = %e\n", ...
	   T, nsteps, dt);
    printf("Number of polynomials D = %d \n", D);
  end

# evaluate the polynomials at the discrete time levels
  pad = zeros(nsteps, D);

  for p=1:D
    q = floor((p-1)/2) + 1;
#    printf("p=%d, q=%d\n", p, q);
    if (q==1)
      tp = T;
      t0 = 0.5*T;
    elseif (q > 1 & q <=4)
      tp = 0.5*T;
      t0 = 0.25*(q-1)*T;
    elseif (q > 4 & q <= 11)
      tp = 0.25*T;
      t0 = 0.125*(q-4)*T;
    elseif (q > 11 & q <= 26)
      tp = 0.125*T;
      t0 = 0.0625*(q-11)*T;
    end
    tau = (td - t0)/tp;
    mask = (tau >= -0.5 & tau <= 0.5);

    if (mod(p,2) == 1)
#      printf("Assigning p=%d, 2*q-1=%d\n", p, 2*q-1);
      pad(:,2*q-1) = 64*mask.*(0.5 + tau).^3 .* (0.5 - tau).^3 .*cos(d1*td);
    else
#      printf("Assigning p=%d, 2*q=%d\n", p, 2*q);
      pad(:,2*q)    = 64*mask.*(0.5 + tau).^3 .* (0.5 - tau).^3 .*sin(d1*td);
    end
  end # for

end