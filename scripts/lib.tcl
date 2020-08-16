proc create_exponential_rv {lambda} {
    set rv [new RandomVariable/Exponential]
    $rv set avg_ [expr 1.0/$lambda]
    return $rv
}

proc create_empirical_rv {cdffile interpolation} {
    set rv [new RandomVariable/Empirical]
    $rv set interpolation_ $interpolation
    $rv loadCDF $cdffile
    return $rv
}

proc create_uniform_rv {min max} {
      set rv [new RandomVariable/Uniform]
      $rv set min_ $min_bytes
      $rv set max_ $max_bytes
      return $rv
}

proc create_fixed_rv {value} {
      set rv [new RandomVariable/Uniform]
      $rv set min_ $value
      $rv set max_ $value
      return $rv
}

proc create_log_normal_rv {avg std} {
    set rv [new RandomVariable/LogNormal]
    $rv set avg_ $avg
    $rv set std_ $std
    return $rv
}

Class FlowData

FlowData instproc init {flow_size} {
    eval $self next {}
    $self instvar size_
    $self set size_ $flow_size
}

FlowData instproc get_total_size {} {
    $self instvar size_
    return $size_
}

