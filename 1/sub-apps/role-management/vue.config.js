const { name } = require('./package');

module.exports = {
  transpileDependencies: true,
  publicPath: '//localhost:8004',
  devServer: {
    port: 8004,
    headers: {
      'Access-Control-Allow-Origin': '*'
    }
  },
  configureWebpack: {
    output: {
      library: `${name}-[name]`,
      libraryTarget: 'umd',
      jsonpFunction: `webpackJsonp_${name}`
    }
  }
};
